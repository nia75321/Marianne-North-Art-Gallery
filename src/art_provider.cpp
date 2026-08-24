/**
 * @file art_provider.cpp
 * @brief 画作来源实现
 */
#include "art_provider.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include "pinout.h"



// 必应每日壁纸接口 (cn.bing.com — 中国大陆可直连)
// 请求地址在 fetchOnlineRandom 里按 http/https 交替构造

/** 从 HTML 字符串里去掉标签, 用于清洗版权声明 */
static String stripHtml(const String& in) {
  String out;
  out.reserve(in.length());
  bool inTag = false;
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    if (c == '<') { inTag = true; continue; }
    if (c == '>') { inTag = false; continue; }
    if (!inTag) out += c;
  }
  out.trim();
  while (out.startsWith(" ")) out.remove(0, 1);
  return out;
}

static bool isJpgName(const String& name) {
  if (name.length() == 0) return false;
  if (name[0] == '.' || name[0] == '_') return false;
  if (name.indexOf("/._") >= 0) return false;
  String l = name;
  l.toLowerCase();
  return l.endsWith(".jpg") || l.endsWith(".jpeg");
}

static String sanitizeFileStem(String s) {
  s.trim();
  if (s.startsWith(".") || s.startsWith("_")) s.remove(0, 1);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (!ok && c != '-' && c != '_') s.setCharAt(i, '_');
  }
  while (s.startsWith("_")) s.remove(0, 1);
  if (s.length() == 0) s = "art";
  if (s.length() > 48) s = s.substring(0, 48);
  return s;
}

bool ArtProvider::begin() {
  // SD 与屏幕共用同一条 SPI 总线, 参照官方 sdcard 示例
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  sdReady_ = SD.begin(SD_CS, SPI, 25000000);
  if (sdReady_) {
    SD.mkdir(ART_SD_ROOT);
    SD.mkdir(ART_SD_CACHE);
    log_i("SD ready");
  } else {
    log_w("SD init failed");
  }
  return sdReady_;
}

static void configureHttp(HTTPClient& http, bool image) {
  http.setTimeout(ART_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0 Safari/537.36");
  if (image) {
    http.addHeader("Accept", "image/avif,image/webp,image/apng,image/*,*/*;q=0.8");
    http.addHeader("Referer", "https://cn.bing.com/");
    http.addHeader("Cache-Control", "no-cache");
  } else {
    http.addHeader("Accept", "application/json,text/plain,*/*");
    http.addHeader("Cache-Control", "no-cache");
  }
}

bool ArtProvider::httpGet(const String& url, String& out, uint32_t timeoutMs) {
  out = "";
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  if (url.startsWith("https")) {
    secureClient_.setInsecure();
    http.begin(secureClient_, url);
  } else {
    http.begin(url);
  }
  http.setTimeout(timeoutMs);
  configureHttp(http, false);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    String body = http.getString();
    log_w("HTTP GET %d url=%s", code, url.c_str());
    if (body.length()) log_w("HTTP body: %s", body.substring(0, 160).c_str());
    http.end();
    return false;
  }
  out = http.getString();
  http.end();
  return true;
}

bool ArtProvider::downloadImage(const String& url, uint8_t** outBuf, size_t* outLen) {
  *outBuf = nullptr;
  *outLen = 0;
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  if (url.startsWith("https")) {
    secureClient_.setInsecure();
    http.begin(secureClient_, url);
  } else {
    http.begin(url);
  }
  configureHttp(http, true);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    log_w("Image GET %d url=%s", code, url.c_str());
    http.end();
    return false;
  }

  int64_t size = http.getSize();
  if (size > (int64_t)ART_MAX_IMAGE_BYTES) {
    log_w("Image size invalid: %lld", size);
    http.end();
    return false;
  }

  // size<0 表示未知长度(chunked), 按上限分配并读到流结束
  size_t cap = (size > 0) ? (size_t)size : (size_t)ART_MAX_IMAGE_BYTES;
  uint8_t* buf = (uint8_t*)ps_malloc(cap);
  if (!buf) {
    log_e("ps_malloc(%u) failed", cap);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t total = 0;
  uint32_t lastDataMs = millis();
  while (total < cap && millis() - lastDataMs < 8000) {
    if (size > 0 && total >= (size_t)size) break;
    int n = stream->read(buf + total, min((int)(cap - total), 2048));
    if (n < 0) break;
    if (n == 0) {
      if (!stream->connected() && stream->available() == 0) break;
      delay(2);
      yield();  // 保持 Wi-Fi 协议栈活跃, 防止看门狗复位
      continue;
    }
    total += n;
    lastDataMs = millis();
  }
  http.end();

  if (size > 0 && total != (size_t)size) {
    log_w("Image download incomplete: %u/%lld", total, size);
    free(buf);
    return false;
  }
  if (size <= 0 && total == 0) {
    log_w("Image download empty");
    free(buf);
    return false;
  }

  if (size < 2 || buf[0] != 0xFF || buf[1] != 0xD8) {
    log_w("Image is not JPEG");
    free(buf);
    return false;
  }

  *outBuf = buf;
  *outLen = size;
  return true;
}

bool ArtProvider::parseBingMeta(const String& payload, Artwork& out, bool useHttp) {
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, payload) != DeserializationError::Ok) {
    log_w("Bing JSON parse failed");
    return false;
  }

  JsonArray images = doc["images"].as<JsonArray>();
  if (images.isNull() || images.size() == 0) return false;

  // 随机挑一张
  int n = (int)images.size();
  JsonObject pick = images[random(n)];
  String urlbase = pick["urlbase"] | "";
  if (urlbase.length() == 0) return false;

  // 国内网络对 HTTPS 长连接节流严重, 优先走 HTTP; 小尺寸 640x360 ≈ 40KB
  String scheme = useHttp ? "http" : "https";
  String imgUrl = scheme + "://cn.bing.com" + urlbase + "_640x360.jpg";

  out.title = stripHtml(pick["title"] | "");
  if (out.title.length() == 0) out.title = "Bing Wallpaper";
  out.artist = stripHtml(pick["copyright"] | "");
  out.year = "";
  out.id = String("bing-") + (const char*)(pick["startdate"] | "");
  out.imagePath = imgUrl;
  out.fromOnline = true;
  return true;
}

bool ArtProvider::parseGalleryListMeta(const String& payload, Artwork& out) {
  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, payload) != DeserializationError::Ok) {
    log_w("Gallery JSON parse failed");
    return false;
  }
  if (!doc.is<JsonArray>()) return false;

  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() == 0) return false;

  // 随机挑一条
  JsonObject pick = arr[random((int)arr.size())];
  String url = pick["image_url"] | "";
  if (url.length() == 0) return false;

  out.title = pick["title"] | "Untitled";
  out.artist = pick["artist"] | "Unknown artist";
  out.year = pick["year"] | "";
  out.id = pick["id"] | url;
  out.imagePath = url;
  out.fromOnline = true;
  return true;
}

bool ArtProvider::fetchOnlineRandom(Artwork& out, uint8_t** jpgBuf, size_t* jpgLen) {
  *jpgBuf = nullptr;
  *jpgLen = 0;
  if (WiFi.status() != WL_CONNECTED) return false;

  constexpr bool kGalleryMode = (sizeof(ART_GALLERY_JSON_URL) > 1);

  // 最多尝试 3 次, 国内网络优先走 HTTP 规避 HTTPS 节流, 失败再换 HTTPS
  for (int attempt = 0; attempt < 3; ++attempt) {
    bool useHttp = (attempt % 2 == 0);

    String metaUrl;
    if (kGalleryMode) {
      metaUrl = ART_GALLERY_JSON_URL;
    } else {
      String scheme = useHttp ? "http" : "https";
      metaUrl = scheme + "://cn.bing.com/HPImageArchive.aspx?format=js&idx=0&n=8";
    }

    String payload;
    if (!httpGet(metaUrl, payload, ART_HTTP_TIMEOUT_MS)) continue;

    bool ok = kGalleryMode ? parseGalleryListMeta(payload, out) : parseBingMeta(payload, out, useHttp);
    if (!ok) continue;

    uint8_t* buf = nullptr;
    size_t len = 0;
    if (!downloadImage(out.imagePath, &buf, &len)) continue;

    cacheToSd(out, buf, len);  // 尽力缓存, 失败不影响主流程
    *jpgBuf = buf;
    *jpgLen = len;
    return true;
  }

  log_w("Online fetch failed after 3 attempts");
  return false;
}

bool ArtProvider::cacheToSd(const Artwork& art, const uint8_t* jpg, size_t len) {
  if (!sdReady_) return false;
  if (art.id.length() == 0 || art.id.indexOf('/') >= 0) return false;

  String base = String(ART_SD_CACHE) + "/" + sanitizeFileStem(art.id);

  File f = SD.open(base + ".jpg", FILE_WRITE);
  if (!f) return false;
  size_t wrote = f.write(jpg, len);
  f.close();
  if (wrote != len) return false;

  DynamicJsonDocument doc(1024);
  doc["title"] = art.title;
  doc["artist"] = art.artist;
  doc["year"] = art.year;
  doc["id"] = art.id;
  File m = SD.open(base + ".json", FILE_WRITE);
  if (m) {
    serializeJson(doc, m);
    m.close();
  }
  return true;
}

void ArtProvider::scanSdDir(const char* dir, String* files, int& count) {
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) return;
  File f;
  while ((f = d.openNextFile()) && count < ART_SD_MAX_FILES) {
    if (!f.isDirectory() && isJpgName(f.name())) {
      files[count++] = String(dir) + "/" + f.name();
    }
    f.close();
  }
  d.close();
}

void ArtProvider::loadMetadata(const String& jpgPath, Artwork& out) {
  String jsonPath = jpgPath;
  int dot = jsonPath.lastIndexOf('.');
  if (dot > 0) jsonPath = jsonPath.substring(0, dot);
  jsonPath += ".json";

  if (SD.exists(jsonPath)) {
    File f = SD.open(jsonPath, FILE_READ);
    if (f) {
      DynamicJsonDocument doc(1024);
      if (deserializeJson(doc, f) == DeserializationError::Ok) {
        out.title = doc["title"] | "";
        out.artist = doc["artist"] | "";
        out.year = doc["year"] | "";
      }
      f.close();
    }
  }

  // 无元数据时用文件名当标题
  if (out.title.length() == 0) {
    String name = jpgPath;
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    int d = name.lastIndexOf('.');
    if (d > 0) name = name.substring(0, d);
    out.title = name;
  }
}

bool ArtProvider::fetchSdRandom(Artwork& out) {
  if (!sdReady_) return false;

  // 600 个 String 对象不能放栈上(会溢出 8KB 任务栈), 用堆分配
  String* files = new (std::nothrow) String[ART_SD_MAX_FILES];
  if (!files) return false;
  int count = 0;
  scanSdDir(ART_SD_ROOT, files, count);
  scanSdDir(ART_SD_CACHE, files, count);
  if (count == 0) {
    log_w("No art under %s", ART_SD_ROOT);
    delete[] files;
    return false;
  }

  String path = files[random(count)];
  delete[] files;

  out.id = path;
  out.imagePath = path;
  out.fromOnline = false;
  loadMetadata(path, out);
  return true;
}
