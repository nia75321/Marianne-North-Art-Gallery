/**
 * @file imu_gesture.cpp
 * @brief 晃动检测实现: 合加速度超过阈值即触发一次晃动
 */
#include "imu_gesture.h"
#include <M5Unified.h>
#include <cmath>
#include "art_config.h"

void ImuGesture::begin() {
#ifdef ART_SHAKE_THRESHOLD_G
  shakeThresholdG_ = ART_SHAKE_THRESHOLD_G;
#endif
}

void ImuGesture::update() {
  float ax, ay, az;
  M5.Imu.getAccel(&ax, &ay, &az);

  // 一阶低通滤波, 消抖
  if (!filtInit_) {
    fx_ = ax;
    fy_ = ay;
    fz_ = az;
    filtInit_ = true;
    return;
  }
  constexpr float kAlpha = 0.25f;
  fx_ += (ax - fx_) * kAlpha;
  fy_ += (ay - fy_) * kAlpha;
  fz_ += (az - fz_) * kAlpha;

  // 合加速度明显偏离 1g(重力) 时视为一次晃动
  float g = sqrtf(fx_ * fx_ + fy_ * fy_ + fz_ * fz_);
  uint32_t now = millis();
  if (g > shakeThresholdG_ && now >= shakeQuietUntil_ && !shake_) {
    shake_ = true;
    shakeQuietUntil_ = now + 1000;  // 晃动后 1s 内不再重复触发
  }
}
