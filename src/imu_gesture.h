/**
 * @file imu_gesture.h
 * @brief 基于 BMI270 加速度计的晃动检测
 *
 * 用法:
 *   ImuGesture imu;
 *   imu.begin();
 *   loop() 中每帧调用 imu.update();
 *   consumeShake() 返回 true 表示一次用力晃动完成。
 */
#pragma once

#include <Arduino.h>

class ImuGesture {
 public:
  void begin();

  void update();

  /** 取事件: 每次用力晃动后返回一次 true */
  bool consumeShake() {
    bool v = shake_;
    shake_ = false;
    return v;
  }

 private:
  bool     filtInit_ = false;
  float    fx_ = 0, fy_ = 0, fz_ = 0;
  bool     shake_ = false;
  float    shakeThresholdG_ = 1.7f;
  uint32_t shakeQuietUntil_ = 0;
};
