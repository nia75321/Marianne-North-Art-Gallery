/**
 * @file main.cpp
 * @brief M5Stack CoreS3 随机艺术画廊
 *
 * 功能:
 *   - 随机展示画作 (联网时从网络获取, 离线从 SD 卡获取)
 *   - IMU 手势: 左倾斜 90° = 上一张, 右倾斜 90° = 下一张
 *   - 触摸辅助: 左半屏 = 上一张, 右半屏 = 下一张
 *   - 自动轮播
 *
 * 配置: include/art_config.h
 */
#include <SPI.h>
#include <SD.h>
#include <HTTPClient.h>
// 注意: SD.h/HTTPClient.h 必须在 M5Unified.h(M5GFX.h) 之前包含
#include <M5Unified.h>
#include <WiFi.h>
#include <time.h>
#include <lwip/inet.h>
#include <esp_log.h>
#include <esp_netif.h>
#include "art_config.h"
#include "art_models.h"
#include "art_provider.h"
#include "imu_gesture.h"
#include "ui.h"

static ArtProvider art;
static ImuGesture imu;

static HistoryEntry hist[ART_HISTORY_SIZE];
static int histCount = 0;

static bool wifiConnected = false;
static uint32_t lastAutoMs = 0;
static uint32_t lastOnlineSyncMs = 0;
static bool randomMode = false;

static void syncClock() {
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
  uint32_t started = millis();
  while (time(nullptr) < 100000 && millis() - started < 3000) {
    delay(100);
  }
}
static uint32_t lastWifiRetryMs = 0;

static void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifiConnected = true;
      log_i("WiFi connected: %s", WiFi.localIP().toString().c_str());
      syncClock();
      lastOnlineSyncMs = 0;  // 连上后立即检查腾讯云
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (wifiConnected) log_w("WiFi disconnected");
      wifiConnected = false;
      lastWifiRetryMs = millis();  // 断开后冷却再重连
      break;
    default:
      break;
  }
}

/* ---------------- 历史记录 ---------------- */

static void freeEntry(HistoryEntry& e) {
  if (e.jpg) {
    free(e.jpg);
    e.jpg = nullptr;
  }
  e.jpgLen = 0;
}

static void pushHistory(const Artwork& a, uint8_t* jpg, size_t len) {
  if (histCount == ART_HISTORY_SIZE) {
    freeEntry(hist[0]);
    for (int i = 1; i < histCount; ++i) hist[i - 1] = hist[i];
    --histCount;
  }
  hist[histCount].art = a;
  hist[histCount].jpg = jpg;
  hist[histCount].jpgLen = len;
  ++histCount;
}

static bool loadSdEntry(HistoryEntry& e) {
  if (e.jpg || e.art.imagePath.length() == 0) return e.jpg != nullptr;
  File file = SD.open(e.art.imagePath, FILE_READ);
  if (!file) return false;
  size_t len = file.size();
  if (len < 4 || len > ART_MAX_IMAGE_BYTES) {
    file.close();
    return false;
  }
  uint8_t* buf = (uint8_t*)ps_malloc(len);
  if (!buf) {
    file.close();
    return false;
  }
  size_t readLen = file.read(buf, len);
  file.close();
  if (readLen != len || buf[0] != 0xFF || buf[1] != 0xD8 ||
      buf[len - 2] != 0xFF || buf[len - 1] != 0xD9) {
    free(buf);
    return false;
  }
  e.jpg = buf;
  e.jpgLen = len;
  return true;
}

static void showEntry(HistoryEntry& e) {
  if (!e.art.fromOnline && !loadSdEntry(e)) {
    ui::showPlaceholder("SD image unavailable");
    return;
  }
  ui::showArtwork(e.art, e.jpg, e.jpgLen, e.art.fromOnline ? "ONLINE" : "SD");
}

/* ---------------- 播放控制 ---------------- */

/** 左倾斜: 上一张 */
static void showPrevious() {
  if (histCount < 2) {
    ui::toast("First artwork");
    return;
  }
  freeEntry(hist[histCount - 1]);  // 丢弃当前, 回退到上一张
  --histCount;
  showEntry(hist[histCount - 1]);
  lastAutoMs = millis();
}

/** 清空历史并重新进入"今天"状态: 关闭随机模式, 从最新缓存画作开始。 */
static void resetToToday() {
  while (histCount > 0) {
    freeEntry(hist[histCount - 1]);
    --histCount;
  }
  randomMode = false;
  art.clearSdLastShown();
}

/** 按日期顺序显示 SD 画作 */
static bool showSdNext() {
  Artwork a;
  if (!art.sdReady() || !art.fetchSdNext(a)) return false;
  pushHistory(a, nullptr, 0);
  showEntry(hist[histCount - 1]);
  return true;
}

/** 显示 SD 中最新的一张 (今天或前一天) */
static bool showSdLatest() {
  Artwork a;
  if (!art.sdReady() || !art.fetchSdLatest(a)) return false;
  pushHistory(a, nullptr, 0);
  showEntry(hist[histCount - 1]);
  return true;
}

/** 随机显示任意一张 SD 画作 */
static bool showSdRandom() {
  Artwork a;
  if (!art.sdReady() || !art.fetchSdRandom(a)) return false;
  pushHistory(a, nullptr, 0);
  showEntry(hist[histCount - 1]);
  return true;
}

/** 用力晃动: 在随机日期 / 按日期顺序两种模式间切换。 */
static void toggleRandomMode() {
  if (randomMode) {
    randomMode = false;
    ui::toast("Date order");
    if (!showSdLatest()) ui::toast("No artwork available");
  } else {
    randomMode = true;
    ui::toast("Random date");
    if (!showSdRandom()) ui::toast("No artwork available");
  }
}

/** 周期性检查腾讯云, 只下载 SD 上没有的新画作 */
static void maybeSyncOnline() {
  if (!wifiConnected) return;
  // GOT_IP 只是驱动事件, 真正的网络就绪需等 DHCP 完成; 避免无 IP 就刷屏 DNS Failed
  {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (!netif || esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0) return;
  }
  if (lastOnlineSyncMs != 0 && millis() - lastOnlineSyncMs < ART_ONLINE_CHECK_MS) return;
  lastOnlineSyncMs = millis();

  log_i("Checking Tencent COS for new paintings...");
  int n = art.syncOnlineGallery(5);
  if (n > 0) {
    log_i("Downloaded %d new painting(s)", n);
    showSdLatest();
  } else if (n == 0) {
    // 明确无新图时才打印 up-to-date; -1 表示网络/DNS 失败, 不刷 up-to-date 误导
    log_i("Gallery already up to date");
  }
}

static void showNext() {
  bool ok = randomMode ? showSdRandom() : showSdNext();
  if (!ok && histCount == 0) ui::toast("No artwork available");
}

/* ---------------- Wi-Fi ---------------- */

static void ensureWifi() {
  if (wifiConnected) return;
  if (millis() - lastWifiRetryMs < ART_WIFI_RETRY_MS) return;
  lastWifiRetryMs = millis();
  WiFi.mode(WIFI_STA);
  // 无需改路由器: 设备端覆盖 DNS, 只改 DNS 不改 IP(由 DHCP 分配)
  {
    IPAddress dns1(223, 5, 5, 5);
    IPAddress dns2(114, 114, 114, 114);
    WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), dns1, dns2);
  }
  WiFi.begin(ART_WIFI_SSID, ART_WIFI_PASSWORD);
}

/* ---------------- 入口 ---------------- */

void setup() {
  auto cfg = M5.config();
  cfg.clear_display = true;
  M5.begin(cfg);

  ui::init();
  ui::showBoot();

  art.begin();
  imu.begin();
  randomSeed(esp_random());

  // Wi-Fi 只在后台连接, 不阻塞 SD 首屏。
  WiFi.mode(WIFI_STA);
  // 驱动级静音 WiFi 事件刷屏(AUTH_EXPIRE/TIMEOUT 等由核心库输出), 保留错误级日志
  esp_log_level_set("WiFiGeneric", ESP_LOG_ERROR);
  // 关闭 WiFi 休眠, 减少掉线; autoReconnect 由驱动在后台退避重连
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onWiFiEvent);
  lastAutoMs = millis();
  lastOnlineSyncMs = 0;  // WiFi 连上后立即检查一次腾讯云
  lastWifiRetryMs = millis() - ART_WIFI_RETRY_MS;  // 开机立即发起首次连接
  if (!showSdLatest()) {
    ui::toast("No artwork available");
  }
  ensureWifi();
}

void loop() {
  M5.update();
  imu.update();

  // 用力晃动: 切换随机日期/按日期顺序模式。
  if (imu.consumeShake()) {
    toggleRandomMode();
  }

  // 触摸左右区域翻页。
  if (M5.Touch.getCount()) {
    const auto& touch = M5.Touch.getDetail();
    if (touch.wasClicked()) {
      if (touch.x < M5.Display.width() / 2) {
        showPrevious();
      } else {
        showNext();
      }
    }
  }

  // Wi-Fi 断线重连; 后台定期检查新画作, 不阻塞 SD 翻页。
  ensureWifi();
  maybeSyncOnline();

  delay(20);
}
