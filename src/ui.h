/**
 * @file ui.h
 * @brief 屏幕渲染模块
 */
#pragma once

#include <Arduino.h>
#include "art_models.h"

namespace ui {

/** 初始化显示 (旋转方向等) */
void init();

/** 开机画面 */
void showBoot();

/** 全屏显示一幅画作 + 底部信息栏 */
void showArtwork(const Artwork& art, const uint8_t* jpg, size_t jpgLen, const char* sourceTag);

/** 将当前大图视口左右移动一段距离。 */
void panArtwork(const Artwork& art, const uint8_t* jpg, size_t jpgLen, const char* sourceTag,
               int deltaX);

/** 无法显示时占位提示 */
void showPlaceholder(const char* msg);

/** 半透明风格 Toast 提示 */
void toast(const char* msg);

}  // namespace ui
