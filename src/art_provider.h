/**
 * @file art_provider.h
 * @brief 画作来源管理
 *
 * 在线: 自定义服务器 或 芝加哥艺术学院公开 API (CC0 公共领域作品)
 * 离线: SD 卡 /art 目录 (自动包含 /art/cache 在线缓存)
 */
#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include "art_config.h"
#include "art_models.h"

class ArtProvider {
 public:
  /** 挂载 SD 卡并创建目录, 返回 SD 是否就绪 */
  bool begin();

  bool sdReady() const { return sdReady_; }

  /**
   * 联网获取随机画作。
   * 成功: 返回 true, *jpgBuf 指向 ps_malloc 分配的 JPEG 缓冲 (调用方用 releaseJpg 释放)。
   * 失败: 返回 false, *jpgBuf 置空。
   */
  bool fetchOnlineRandom(Artwork& out, uint8_t** jpgBuf, size_t* jpgLen);

  /** 从 SD 卡随机选一张画作 (元数据读同名 .json, 缺省用文件名) */
  bool fetchSdRandom(Artwork& out);

  /** 从 SD 卡按文件名顺序取下一张画作 */
  bool fetchSdNext(Artwork& out);

  static void releaseJpg(uint8_t* buf) {
    if (buf) free(buf);
  }

 private:
  bool httpGet(const String& url, String& out, uint32_t timeoutMs);
  bool downloadImage(const String& url, uint8_t** outBuf, size_t* outLen);
  bool rangeChunkDownload(const String& url, uint8_t* buf, size_t cap);
  bool parseBingMeta(const String& payload, Artwork& out, bool useHttp);
  bool parseGalleryListMeta(const String& payload, Artwork& out);
  bool cacheToSd(const Artwork& art, const uint8_t* jpg, size_t len);
  void scanSdDir(const char* dir, String* files, int& count);
  void loadMetadata(const String& jpgPath, Artwork& out);

  WiFiClientSecure secureClient_;
  bool sdReady_ = false;
  String sdLastShown_;  // 最近一次显示的 SD 图片路径, 用于按时间顺序翻页
};
