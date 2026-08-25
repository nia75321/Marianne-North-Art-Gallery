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
static bool dailyOnlineAttempted = false;

static int currentDay() {
  time_t now = time(nullptr);
  struct tm local{};
  if (now < 100000) return -1;
  localtime_r(&now, &local);
  return local.tm_yday;
}

static String dailyMarkerPath() {
  time_t now = time(nullptr);
  struct tm local{};
  if (now < 100000) return "";
  localtime_r(&now, &local);
  char name[48];
  snprintf(name, sizeof(name), "%s/daily-%04d%02d%02d.done", ART_SD_CACHE,
           local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
  return String(name);
}

static bool downloadedToday() {
  String marker = dailyMarkerPath();
  return marker.length() > 0 && SD.exists(marker);
}

static void markDownloadedToday() {
  String marker = dailyMarkerPath();
  if (marker.length() == 0 || !art.sdReady()) return;
  File f = SD.open(marker, FILE_WRITE);
  if (f) {
    f.print("ok");
    f.close();
  }
}

static void syncClock() {
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
  uint32_t started = millis();
  while (time(nullptr) < 100000 && millis() - started < 3000) {
    delay(100);
  }
}
static uint32_t lastWifiRetryMs = 0;

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

/** 右倾斜 / 自动轮播: 随机下一张 (联网优先, 否则 SD) */
static bool showSdRandom() {
  Artwork a;
  if (!art.sdReady() || !art.fetchSdRandom(a)) return false;
  pushHistory(a, nullptr, 0);
  showEntry(hist[histCount - 1]);
  return true;
}

static void tryDailyOnline() {
  if (WiFi.status() != WL_CONNECTED || dailyOnlineAttempted) return;
  int day = currentDay();
  if (day < 0 || downloadedToday()) return;

  dailyOnlineAttempted = true;
  Artwork a;
  uint8_t* jpg = nullptr;
  size_t len = 0;
  if (art.fetchOnlineRandom(a, &jpg, &len)) {
    markDownloadedToday();
    // 在线缓冲已成功写入 SD; 释放 RAM 后从文件显示，避免大图缓冲解码失败。
    art.releaseJpg(jpg);
    showSdRandom();
  } else {
    art.releaseJpg(jpg);
  }
}

static void showNextRandom() {
  if (!showSdRandom()) {
    tryDailyOnline();
    if (histCount == 0) ui::toast("No artwork available");
  }
}

/* ---------------- Wi-Fi ---------------- */

static void ensureWifi() {
  if (wifiConnected) return;
  if (millis() - lastWifiRetryMs < 30000) return;
  lastWifiRetryMs = millis();

  WiFi.begin(ART_WIFI_SSID, ART_WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 5000) {
    delay(100);
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) {
    log_i("WiFi connected: %s", WiFi.localIP().toString().c_str());
    syncClock();
    tryDailyOnline();
  }
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
  lastAutoMs = millis();
  if (!showSdRandom()) {
    ui::toast("No artwork available");
  }
  ensureWifi();
  tryDailyOnline();
}

void loop() {
  M5.update();
  imu.update();

  // 仅使用触摸左右区域翻页。
  if (M5.Touch.getCount()) {
    const auto& touch = M5.Touch.getDetail();
    if (touch.wasClicked()) {
      if (touch.x < M5.Display.width() / 2) {
        showPrevious();
      } else {
        showNextRandom();
      }
    }
  }

  // Wi-Fi 断线重连; 在线更新永远不阻塞 SD 首屏。
  ensureWifi();
  tryDailyOnline();

  delay(20);
}
