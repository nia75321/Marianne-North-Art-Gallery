/**
 * @file imu_gesture.cpp
 * @brief 加速度计倾斜检测实现
 */
#include "imu_gesture.h"
#include <M5Unified.h>
#include <cmath>
#include "art_config.h"

static constexpr float kRadToDeg = 57.295779513f;

void ImuGesture::begin() {
  // M5.begin() 已初始化 BMI270, 无需额外配置
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

  // 合加速度明显偏离 1g 时忽略 (快速晃动 / 跌落)
  float g = sqrtf(fx_ * fx_ + fy_ * fy_ + fz_ * fz_);
  if (g < 0.5f || g > 1.6f) return;

  // ---- 计算倾斜角度 ----
  float angle;
#if ART_IMU_PLANE == 1
  // 竖立(支架/手持): 滚转角 = atan2(az, -ay)
  // 0°=直立, 正值=右倾, 负值=左倾
  angle = atan2f(fz_, -fy_) * kRadToDeg;
#else
  // 平放桌面: 滚转角 = atan2(ax, az)
  angle = atan2f(fx_, fz_) * kRadToDeg;
#endif
#if ART_TILT_SIGN_INVERT == 1
  angle = -angle;
#endif

  // ---- 状态机 ----
  const float trigger = ART_TILT_TRIGGER_DEG;
  const float release = ART_TILT_RELEASE_DEG;
  uint32_t now = millis();

  switch (state_) {
    case State::kFlat:
      if (angle < -trigger) {
        state_ = State::kTiltLeft;
        since_ = now;
      } else if (angle > +trigger) {
        state_ = State::kTiltRight;
        since_ = now;
      }
      break;

    case State::kTiltLeft:
      if (angle > -release) {
        state_ = State::kFlat;  // 已回正, 重新武装
      } else if (now - since_ >= ART_TILT_HOLD_MS) {
        state_ = State::kFlat;
        if (now >= cooldownUntil_) {
          left_ = true;
          cooldownUntil_ = now + ART_TILT_COOLDOWN_MS;
        }
      }
      break;

    case State::kTiltRight:
      if (angle < +release) {
        state_ = State::kFlat;
      } else if (now - since_ >= ART_TILT_HOLD_MS) {
        state_ = State::kFlat;
        if (now >= cooldownUntil_) {
          right_ = true;
          cooldownUntil_ = now + ART_TILT_COOLDOWN_MS;
        }
      }
      break;
  }
}