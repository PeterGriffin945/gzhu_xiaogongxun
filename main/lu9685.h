/**
 * @file lu9685.h
 * @brief LU9685 16 路舵机驱动与连续舵机安全点动
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LU9685_PROTO_ANGLE = 0,
    LU9685_PROTO_PULSE_LO = 1,
    LU9685_PROTO_PULSE_HI = 2,
} lu9685_proto_t;

/* 舵机控制模式：位置舵机（MG996 类）写一次角度即到位保持；
   连续旋转舵机（物料盘）才使用定时点动。 */
typedef enum {
    LU9685_MODE_POSITION = 0,
    LU9685_MODE_CONTINUOUS = 1,
} lu9685_mode_t;

esp_err_t lu9685_set_mode(uint8_t channel, lu9685_mode_t mode);
lu9685_mode_t lu9685_get_mode(uint8_t channel);

/** 逻辑角度软限位（度）。位置舵机所有运动都会被限制在该区间内。 */
esp_err_t lu9685_set_limits_deg(uint8_t channel, float min_deg, float max_deg);
void lu9685_get_limits_deg(uint8_t channel, float *min_deg, float *max_deg);

/** 位置舵机：走到指定逻辑角度（度）并保持；连续舵机返回错误。 */
esp_err_t lu9685_move_deg(uint8_t channel, float deg);

/** 位置舵机：在当前逻辑角度上相对步进 delta_deg（例如 ±15°）。 */
esp_err_t lu9685_step_deg(uint8_t channel, float delta_deg);

/** 当前逻辑角度（度）。 */
float lu9685_get_pos_deg(uint8_t channel);

esp_err_t lu9685_init(void);
bool lu9685_available(void);
bool lu9685_pulse_ready(void);
uint8_t lu9685_address(void);
esp_err_t lu9685_set_protocol(uint8_t mode);
uint8_t lu9685_get_protocol(void);
esp_err_t lu9685_set_angle(uint8_t channel, uint8_t angle);
uint8_t lu9685_get_angle(uint8_t channel);
esp_err_t lu9685_set_center(uint8_t channel, uint8_t angle);
uint8_t lu9685_get_center(uint8_t channel);
esp_err_t lu9685_pulse_start(uint8_t channel, uint8_t speed_angle,
                             uint32_t duration_ms, int stop_angle);

/**
 * @brief 连续旋转舵机的“定时定位”：按目标角度差换算转动时间
 *
 * 内部维护每个通道的逻辑角度（默认中位 90），只转动 |target-current|
 * 对应的时间后自动停止，不会一直转。
 *
 * @param channel        通道
 * @param target_angle   目标逻辑角度 0~180
 * @param speed_angle    转动速度角：<90 正转、>90 反转（建议 40~80）
 * @param ms_per_180     逻辑 0~180° 行程所需时间 ms
 * @param stop_angle     停止角，<0 使用该通道中位
 * @param duration_ms_out 可选，返回本次实际转动时间
 */
esp_err_t lu9685_move_timed(uint8_t channel, uint16_t target_angle,
                            uint8_t speed_angle, uint32_t ms_per_180,
                            int stop_angle, uint32_t *duration_ms_out);

/** 通道当前逻辑角度（连续通道为定时点动累积值） */
uint16_t lu9685_get_target_angle(uint8_t channel);

/** 直接声明逻辑角度（不转动）。用于开赛前把“底座当前姿态”标为运输角。 */
esp_err_t lu9685_set_target_angle(uint8_t channel, uint16_t angle);
esp_err_t lu9685_pulse_cancel(uint8_t channel);
bool lu9685_pulse_active(uint8_t channel);
bool lu9685_pulse_faulted(uint8_t channel);
void lu9685_pulse_stop_all(void);

/** 急停：取消所有定时器，位置舵机保持当前角度（不产生新动作）。 */
void lu9685_hold_all(void);

#ifdef __cplusplus
}
#endif
