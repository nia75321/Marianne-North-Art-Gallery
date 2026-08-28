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

static void configureHttp(HTTPClient& http, bool image, const String& url) {
  http.setTimeout(ART_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0 Safari/537.36");
  if (image) {
    http.addHeader("Accept", "image/avif,image/webp,image/apng,image/*,*/*;q=0.8");
    // Referer 仅对 Bing 图片需要; 对 Gitee 等图库源会触发防盗链 403
    if (url.indexOf("bing.com") >= 0) {
      http.addHeader("Referer", "https://cn.bing.com/");
    }
    http.addHeader("Cache-Control", "no-cache");
  } else {
    http.addHeader("Accept", "application/json,text/plain,*/*");
    http.addHeader("Cache-Control", "no-cache");
  }
}

bool ArtProvider::httpGet(const String& url, String& out, uint32_t timeoutMs) {
  out = "";
  if (WiFi.status() != WL_CONNECTED) return false;

  // 公开 COS 支持 HTTP; HTTPS/DNS 抖动时回退 HTTP，避免 TLS 握手失败。
  String attempts[2] = {url, url};
  if (url.startsWith("https://") && url.indexOf(".myqcloud.com") >= 0) {
    attempts[1].replace("https://", "http://");
  }

  for (int attempt = 0; attempt < 2; ++attempt) {
    if (attempt == 1 && attempts[1] == attempts[0]) break;
    HTTPClient http;
    String requestUrl = attempts[attempt];
    if (requestUrl.startsWith("https")) {
      secureClient_.setInsecure();
      http.begin(secureClient_, requestUrl);
    } else {
      http.begin(requestUrl);
    }
    http.setTimeout(timeoutMs);
    configureHttp(http, false, requestUrl);

    int code = http.GET();
    if (code == HTTP_CODE_OK) {
      out = http.getString();
      http.end();
      if (attempt == 1) log_i("COS metadata HTTP fallback succeeded");
      return true;
    }
    String body = http.getString();
    log_w("HTTP GET %d url=%s", code, requestUrl.c_str());
    if (body.length()) log_w("HTTP body: %s", body.substring(0, 160).c_str());
    http.end();
  }
  return false;
}

// 从流中读取最多 want 字节; 服务器断连/8秒无数据则提前返回
static size_t readStream(WiFiClient* stream, uint8_t* dst, size_t want) {
  size_t got = 0;
  uint32_t lastDataMs = millis();
  while (got < want && millis() - lastDataMs < 8000) {
    int n = stream->read(dst + got, min((int)(want - got), 2048));
    if (n < 0) break;
    if (n == 0) {
      if (!stream->connected() && stream->available() == 0) break;
      delay(2);
      yield();
      continue;
    }
    got += n;
    lastDataMs = millis();
  }
  return got;
}

bool ArtProvider::downloadImage(const String& url, uint8_t** outBuf, size_t* outLen) {
  if (downloadImageOnce(url, outBuf, outLen)) return true;
  if (url.startsWith("https://") && url.indexOf(".myqcloud.com") >= 0) {
    String fallback = url;
    fallback.replace("https://", "http://");
    log_i("Retrying COS image over HTTP");
    return downloadImageOnce(fallback, outBuf, outLen);
  }
  return false;
}

bool ArtProvider::downloadImageOnce(const String& url, uint8_t** outBuf, size_t* outLen) {
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
  configureHttp(http, true, url);

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

  size_t total = readStream(http.getStreamPtr(), buf, cap);
  http.end();

  if (total != cap && size > 0) {
    // 单连接被截断(COS/Gitee/TLS 记录大小), 改用 Range 小分块拼接
    log_w("Image cut at %u/%u, resuming with Range chunks", total, cap);
    if (!rangeChunkDownload(url, buf, cap)) {
      free(buf);
      return false;
    }
    total = cap;
  }

  if (size > 0 && total != cap) {
    log_w("Image download incomplete: %u/%u", total, cap);
    free(buf);
    return false;
  }
  if (size <= 0 && total == 0) {
    log_w("Image download empty");
    free(buf);
    return false;
  }

  if (cap < 2 || buf[0] != 0xFF || buf[1] != 0xD8) {
    log_w("Image is not JPEG");
    free(buf);
    return false;
  }

  *outBuf = buf;
  *outLen = cap;
  return true;
}

// 部分服务器(COS/Gitee)单连接传输会被截断在几 KB~16KB,
// 用 6KB 的 Range 小分块绕过, 每个分块独立连接且小于截断阈值.
// COS/nginx 对静态文件原生支持 Range 响应 206 Partial Content.
bool ArtProvider::rangeChunkDownload(const String& url, uint8_t* buf, size_t cap) {
  const size_t CHUNK = 6144;
  size_t offset = 0;
  while (offset < cap) {
    size_t end = offset + CHUNK - 1;
    if (end >= cap) end = cap - 1;
    char range[40];
    snprintf(range, sizeof(range), "bytes=%u-%u", (unsigned)offset, (unsigned)end);
    size_t want = end - offset + 1;

    bool gotChunk = false;
    for (int retry = 0; retry < 2 && !gotChunk; ++retry) {
      HTTPClient http;
      if (url.startsWith("https")) {
        secureClient_.setInsecure();
        http.begin(secureClient_, url);
      } else {
        http.begin(url);
      }
      configureHttp(http, true, url);
      http.addHeader("Range", range);

      int code = http.GET();
      if (code != HTTP_CODE_PARTIAL_CONTENT) {
        log_w("Range %s -> HTTP %d", range, code);
        http.end();
        continue;
      }
      size_t got = readStream(http.getStreamPtr(), buf + offset, want);
      http.end();
      if (got == want) {
        gotChunk = true;
      } else {
        log_w("Range chunk %s incomplete: %u/%u", range, got, want);
      }
    }
    if (!gotChunk) return false;
    offset += want;
  }
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

  // 云端索引已按时间倒序, 每次在线更新取第一条(最新触发的画作)。
  JsonObject pick = arr[0];
  String url = pick["image_url"] | "";
  if (url.length() == 0) return false;

  // 相对路径(仅文件名)时, 以 gallery.json 所在目录为基础拼接
  if (!url.startsWith("http://") && !url.startsWith("https://")) {
    String base = ART_GALLERY_JSON_URL;
    int slash = base.lastIndexOf('/');
    if (slash >= 0) base = base.substring(0, slash + 1);
    url = base + url;
  }

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

  // 最多尝试 3 次 (Bing 模式国内网络优先 HTTP 规避 HTTPS 节流; 图库模式直接用配置 URL)
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

static String cacheKeyForArtwork(const Artwork& art) {
  String key = art.id;
  if (key.length() == 0) key = art.imagePath;
  if (key.length() > 80) {
    // URL 过长时取末尾文件名, 例如 painting-20260824.jpg
    int slash = key.lastIndexOf('/');
    if (slash >= 0) key = key.substring(slash + 1);
  }
  return sanitizeFileStem(key);
}

bool ArtProvider::cacheToSd(const Artwork& art, const uint8_t* jpg, size_t len) {
  if (!sdReady_ || !jpg || len == 0) return false;

  String base = String(ART_SD_CACHE) + "/" + cacheKeyForArtwork(art);

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
      String path = f.name();
      // Arduino-ESP32 3.x may return an absolute name from openNextFile().
      // Do not prepend dir twice (e.g. /art/cache//art/cache/photo.jpg).
        if (!path.startsWith("/")) path = String(dir) + "/" + path;
      // openNextFile() can return /art/cache/file.jpg even while scanning /art;
      // only accept files that are actually inside the directory being scanned.
      String prefix = String(dir) + "/";
      if (!path.startsWith(prefix)) continue;
      files[count++] = path;
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

static bool isValidJpegFile(const String& path) {
  File image = SD.open(path, FILE_READ);
  if (!image || image.size() < 4) {
    if (image) image.close();
    return false;
  }
  size_t size = image.size();
  uint8_t head[2] = {0, 0};
  image.read(head, sizeof(head));
  image.seek(size - 2);
  uint8_t tail[2] = {0, 0};
  image.read(tail, sizeof(tail));
  image.close();
  return head[0] == 0xFF && head[1] == 0xD8 &&
         tail[0] == 0xFF && tail[1] == 0xD9;
}

bool ArtProvider::fetchSdRandom(Artwork& out) {
  if (!sdReady_) return false;
  String* files = new (std::nothrow) String[ART_SD_MAX_FILES];
  if (!files) return false;
  int count = 0;
  scanSdDir(ART_SD_CACHE, files, count);
  if (count == 0) {
    log_w("No art under %s", ART_SD_ROOT);
    delete[] files;
    return false;
  }
  for (int attempt = 0; attempt < count; ++attempt) {
    int index = random(count);
    String path = files[index];
    if (!isValidJpegFile(path)) continue;
    delete[] files;
    out.id = path;
    out.imagePath = path;
    out.fromOnline = false;
    loadMetadata(path, out);
    return true;
  }
  delete[] files;
  log_w("No valid JPEG under %s", ART_SD_ROOT);
  return false;
}

bool ArtProvider::fetchSdNext(Artwork& out) {
  if (!sdReady_) return false;
  String* files = new (std::nothrow) String[ART_SD_MAX_FILES];
  if (!files) return false;
  int count = 0;
  scanSdDir(ART_SD_CACHE, files, count);
  if (count == 0) {
    log_w("No art under %s", ART_SD_ROOT);
    delete[] files;
    return false;
  }

  // 文件名按 painting-YYYYMMDD-HHMMSS.jpg 的日期和时间排序。
  // 直接找严格大于上一张的最小文件名，不依赖目录枚举顺序。
  String lastShown = sdLastShown_;
  String best = "";
  for (int i = 0; i < count; ++i) {
    if (files[i].length() == 0 || !files[i].startsWith(String(ART_SD_CACHE) + "/")) continue;
    if (lastShown.length() == 0 || files[i] > lastShown) {
      if (best.length() == 0 || files[i] < best) best = files[i];
    }
  }
  if (best.length() == 0) {
    // 找不到更晚的文件时回到最早的一张。
    for (int i = 0; i < count; ++i) {
      if (files[i].length() == 0 || !files[i].startsWith(String(ART_SD_CACHE) + "/")) continue;
      if (best.length() == 0 || files[i] < best) best = files[i];
    }
  }
  delete[] files;
  if (best.length() == 0) {
    log_w("No art under %s", ART_SD_ROOT);
    return false;
  }
  if (!isValidJpegFile(best)) {
    log_w("No valid JPEG under %s", ART_SD_ROOT);
    return false;
  }
  sdLastShown_ = best;
  out.id = best;
  out.imagePath = best;
  out.fromOnline = false;
  loadMetadata(best, out);
  return true;
}

bool ArtProvider::fetchSdLatest(Artwork& out) {
  if (!sdReady_) return false;
  String* files = new (std::nothrow) String[ART_SD_MAX_FILES];
  if (!files) return false;
  int count = 0;
  scanSdDir(ART_SD_CACHE, files, count);

  String best = "";
  for (int i = 0; i < count; ++i) {
    if (files[i].length() == 0 || !files[i].startsWith(String(ART_SD_CACHE) + "/")) continue;
    if (best.length() == 0 || files[i] > best) best = files[i];
  }
  delete[] files;
  if (best.length() == 0 || !isValidJpegFile(best)) {
    log_w("No valid JPEG under %s", ART_SD_ROOT);
    return false;
  }
  sdLastShown_ = best;
  out.id = best;
  out.imagePath = best;
  out.fromOnline = false;
  loadMetadata(best, out);
  return true;
}

int ArtProvider::syncOnlineGallery(int maxNew) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  if (sizeof(ART_GALLERY_JSON_URL) <= 1) return 0;

  String payload;
  if (!httpGet(ART_GALLERY_JSON_URL, payload, ART_HTTP_TIMEOUT_MS)) return -1;

  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return -1;
  if (!doc.is<JsonArray>()) return -1;

  // 相对路径(仅文件名)时, 以 gallery.json 所在目录为基础拼接
  String base = ART_GALLERY_JSON_URL;
  int slash = base.lastIndexOf('/');
  if (slash >= 0) base = base.substring(0, slash + 1);

  int downloaded = 0;
  int attempted = 0;
  int failed = 0;
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject pick : arr) {
    if (downloaded >= maxNew) break;

    String url = pick["image_url"] | "";
    if (url.length() == 0) continue;
    if (!url.startsWith("http://") && !url.startsWith("https://")) url = base + url;

    String filename = url;
    int ls = filename.lastIndexOf('/');
    if (ls >= 0) filename = filename.substring(ls + 1);

    // 已缓存的画作跳过, 只下载新出现的
    String cachePath = String(ART_SD_CACHE) + "/" + sanitizeFileStem(filename);
    if (SD.exists(cachePath + ".jpg")) continue;

    ++attempted;
    Artwork a;
    a.title = pick["title"] | "Untitled";
    a.artist = pick["artist"] | "Unknown artist";
    a.year = pick["year"] | "";
    a.id = filename;
    a.imagePath = url;
    a.fromOnline = true;

    uint8_t* buf = nullptr;
    size_t len = 0;
    if (!downloadImage(url, &buf, &len)) { ++failed; continue; }
    cacheToSd(a, buf, len);
    free(buf);
    ++downloaded;
    log_i("Synced new painting: %s", filename.c_str());
  }
  // attempted==0: 真的已是最新; downloaded==0&&failed>0: 网络失败, 返回 -1 避免误报 up-to-date
  if (attempted == 0) return 0;
  if (downloaded == 0 && failed > 0) return -1;
  return downloaded;
}
