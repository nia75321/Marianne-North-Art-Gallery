/**
 * @file art_models.h
 * @brief 画作数据模型
 */
#pragma once

#include <Arduino.h>

/** 一幅画作 */
struct Artwork {
  String id;         /* 唯一标识: 在线=来源id; 离线=文件路径 */
  String title;
  String artist;
  String year;
  String imagePath;  /* 在线: 图片 URL; 离线: SD 卡文件路径 */
  bool   fromOnline = false;
};

/** 历史记录条目 (保留 JPEG 缓冲, 供"上一张"快速回看) */
struct HistoryEntry {
  Artwork  art;
  uint8_t* jpg    = nullptr;
  size_t   jpgLen = 0;
};
