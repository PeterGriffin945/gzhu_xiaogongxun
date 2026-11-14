/**
 * @file stepper.h
 * @brief 5 轴 DRV8825 步进电机 DDS 脉冲发生器 (20kHz 硬件定时器 + 快速梯形加减速)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AXIS_RR   = 0,   /* 电机1 右后 */
    AXIS_RF   = 1,   /* 电机2 右前 */
    AXIS_LF   = 2,   /* 电机3 左前 */
    AXIS_LR   = 3,   /* 电机4 左后 */
    AXIS_LIFT = 4,   /* 电机5 升降 */
    AXIS_COUNT = 5
} stepper_axis_t;

/** 初始化 GPIO、20kHz GPTimer 与 1kHz 斜坡任务 */
esp_err_t stepper_init(void);

/** 设置单轴目标速度 (步/秒)。内部自动完成起步死区跨越与梯形加减速 */
void stepper_set_speed(stepper_axis_t axis, float steps_per_sec);

/** 一次性设置 4 个底盘轮目标速度 (步/秒)，顺序 RR/RF/LF/LR */
void stepper_set_chassis_speeds(const float steps_per_sec[4]);

/** 读取当前实际速度 (步/秒，含斜坡) */
float stepper_get_speed(stepper_axis_t axis);

/** 读取目标速度 (步/秒) */
float stepper_get_target(stepper_axis_t axis);

/** 设置加速度 (步/秒²)，对所有轴生效 */
void stepper_set_accel(float steps_per_sec2);
float stepper_get_accel(void);

/** 运行时翻转某轴 DIR 逻辑（调试用） */
void stepper_set_dir_invert(stepper_axis_t axis, bool invert);
bool stepper_get_dir_invert(stepper_axis_t axis);

/** 底盘 / 升降使能：true = 锁轴(EN=低)，false = 脱机释放(EN=高) */
void stepper_set_chassis_enable(bool enable);
void stepper_set_lift_enable(bool enable);
bool stepper_chassis_enabled(void);
bool stepper_lift_enabled(void);

/** 停车：目标速度清零，按当前加速度减速到 0 */
void stepper_stop(stepper_axis_t axis);
void stepper_stop_all(void);

/** 立即停车（清空斜坡，用于急停） */
void stepper_abort_all(void);

#ifdef __cplusplus
}
#endif
