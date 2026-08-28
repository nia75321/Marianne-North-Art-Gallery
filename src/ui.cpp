/**
 * @file ui.cpp
 * @brief 屏幕渲染实现
 */
#include "ui.h"
#include <SPI.h>
#include <SD.h>
// 注意: SD.h 必须在 M5Unified.h(M5GFX.h) 之前包含,
// 否则 drawJpgFile(fs::FS&) 等文件系统重载不会被声明
#include <M5Unified.h>
#include "art_config.h"

static M5Canvas canvas(&M5.Display);

/* ---------------- 辅助 ---------------- */

/** 按像素宽度截断文本 (多出的用 ... 代替) */
static String fitText(const char* s, int maxPx) {
  String t = s;
  while (t.length() > 0 && M5.Display.textWidth(t.c_str()) > maxPx) {
    t = t.substring(0, t.length() - 1);
  }
  if (t.length() < String(s).length()) t += "...";
  return t;
}

/* ---------------- 公共 API ---------------- */

void ui::init() {
  M5.Display.setRotation(ART_SCREEN_ROTATION);
  M5.Display.fillScreen(TFT_BLACK);
  log_i("Screen: %dx%d", M5.Display.width(), M5.Display.height());
}

void ui::showBoot() {
  const int W = M5.Display.width();
  const int H = M5.Display.height();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setFont(&fonts::Font4);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("Art Gallery", W / 2, H / 2 - 40);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString("Marianne North", W / 2, H / 2 - 8);
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.drawString("M5Stack CoreS3", W / 2, H / 2 + 22);
}

void ui::toast(const char* msg) {
  const int W = M5.Display.width();
  const int H = M5.Display.height();
  M5.Display.setFont(&fonts::Font2);
  int tw = M5.Display.textWidth(msg);
  int x0 = (W - tw) / 2 - 8;
  int y0 = H - 72;
  M5.Display.fillRoundRect(x0, y0, tw + 16, 24, 6, TFT_NAVY);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Display.drawString(msg, W / 2, y0 + 12);
}

void ui::showPlaceholder(const char* msg) {
  const int W = M5.Display.width();
  const int H = M5.Display.height();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.drawString("Art Gallery", W / 2, H / 2 - 30);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.drawString(msg, W / 2, H / 2 + 6);
}

static bool drawCurrentImage(const Artwork& art, const uint8_t* jpg, size_t jpgLen) {
  const int W = M5.Display.width();
  const int H = M5.Display.height();
  const int imageWidth = W;
  const int imageHeight = H;
  bool drawn = false;
  if (jpg && jpgLen) {
    drawn = M5.Display.drawJpg(jpg, jpgLen, 0, 0, imageWidth, imageHeight,
                               0, 0, 1.0f, 1.0f, MC_DATUM);
  } else {
    drawn = M5.Display.drawJpgFile(art.imagePath.c_str(), 0, 0, imageWidth,
                                   imageHeight, 0, 0, 1.0f, 1.0f, MC_DATUM);
  }
  return drawn;
}

void ui::showArtwork(const Artwork& art, const uint8_t* jpg, size_t jpgLen,
                     const char* sourceTag) {
  const int W = M5.Display.width();
  const int H = M5.Display.height();
  M5.Display.fillScreen(TFT_BLACK);

  bool drawn = drawCurrentImage(art, jpg, jpgLen);
  if (!drawn) {
    showPlaceholder("Decode failed");
    return;
  }

  // 2. 底部信息栏: 单行标题+年份, 缩小遮挡
  const int barH = 20;
  M5.Display.fillRect(0, H - barH, W, barH, TFT_BLACK);
  M5.Display.drawFastHLine(0, H - barH, W, TFT_DARKGREY);

  String line = art.title.length() ? art.title : String("(Untitled)");
  if (art.year.length()) line += String("  ") + art.year;
  if (sourceTag && String(sourceTag).length() > 0) {
    line += String("  ") + String(sourceTag);
  }
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString(fitText(line.c_str(), W - 8), W / 2, H - barH / 2 - 1);
}

