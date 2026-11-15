/**
 * @file stepper.h
 * @brief 5 轴 DRV8825 DDS 脉冲、实际发步计数与开环定步运动
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AXIS_RR = 0,
    AXIS_RF = 1,
    AXIS_LF = 2,
    AXIS_LR = 3,
    AXIS_LIFT = 4,
    AXIS_COUNT = 5
} stepper_axis_t;

typedef enum {
    STEPPER_MOVE_IDLE = 0,
    STEPPER_MOVE_RUNNING,
    STEPPER_MOVE_DONE,
    STEPPER_MOVE_CANCELLED,
} stepper_move_status_t;

esp_err_t stepper_init(void);
void stepper_set_speed(stepper_axis_t axis, float steps_per_sec);
void stepper_set_chassis_speeds(const float steps_per_sec[4]);
float stepper_get_speed(stepper_axis_t axis);
float stepper_get_target(stepper_axis_t axis);
void stepper_set_accel(float steps_per_sec2);
float stepper_get_accel(void);
void stepper_set_dir_invert(stepper_axis_t axis, bool invert);
bool stepper_get_dir_invert(stepper_axis_t axis);
void stepper_set_chassis_enable(bool enable);
void stepper_set_lift_enable(bool enable);
bool stepper_chassis_enabled(void);
bool stepper_lift_enabled(void);

/**
 * 开环定步运动：位置来自 ISR 实际发出的 STEP 脉冲，不代表机械反馈。
 * 到位前按剩余脉冲限制速度，最后一个脉冲后立即封锁后续发步。
 */
esp_err_t stepper_start_move(stepper_axis_t axis, int32_t delta_steps, float peak_sps);
stepper_move_status_t stepper_get_move_status(stepper_axis_t axis);
bool stepper_move_active(stepper_axis_t axis);
int32_t stepper_get_move_remaining(stepper_axis_t axis);
int32_t stepper_get_position(stepper_axis_t axis);
esp_err_t stepper_set_position(stepper_axis_t axis, int32_t position);

void stepper_stop(stepper_axis_t axis);
void stepper_stop_all(void);
void stepper_abort_all(void);

#ifdef __cplusplus
}
#endif
