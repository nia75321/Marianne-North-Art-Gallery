/**
 * @file imu_gesture.h
 * @brief 基于 BMI270 加速度计的左右倾斜手势检测
 *
 * 用法:
 *   ImuGesture imu;
 *   imu.begin();
 *   loop() 中每帧调用 imu.update();
 *   consumeTiltLeft()/consumeTiltRight() 返回 true 表示一次手势完成。
 */
#pragma once

#include <Arduino.h>

class ImuGesture {
 public:
  void begin();

  void update();

  /** 取事件: 每次倾斜触发后返回一次 true */
  bool consumeTiltLeft() {
    bool v = left_;
    left_ = false;
    return v;
  }
  bool consumeTiltRight() {
    bool v = right_;
    right_ = false;
    return v;
  }

 private:
  enum class State : uint8_t { kFlat, kTiltLeft, kTiltRight };

  bool     filtInit_ = false;
  float    fx_ = 0, fy_ = 0, fz_ = 0;
  State    state_ = State::kFlat;
  uint32_t since_ = 0;
  uint32_t cooldownUntil_ = 0;
  bool     left_ = false;
  bool     right_ = false;
};
