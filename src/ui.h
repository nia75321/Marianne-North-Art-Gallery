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

/** 单击切换适合屏幕/放大查看模式。 */
void toggleZoom(const Artwork& art, const uint8_t* jpg, size_t jpgLen, const char* sourceTag);

/** 无法显示时占位提示 */
void showPlaceholder(const char* msg);

/** 半透明风格 Toast 提示 */
void toast(const char* msg);

}  // namespace ui
