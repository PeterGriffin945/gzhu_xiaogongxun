/**
 * @file chassis.h
 * @brief 底盘手动/循迹/定步平移与升降统一控制仲裁
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CHASSIS_MODE_MANUAL = 0,
    CHASSIS_MODE_LINE = 1,
    CHASSIS_MODE_POSITION = 2,
} chassis_mode_t;

typedef struct {
    chassis_mode_t mode;
    float vx, vy, omega;
    float wheel_mps[4];
    int32_t wheel_steps[4];
    float line_error;
    bool line_seen;
    float lift_sps;
    uint32_t sensor_seq;
} chassis_status_t;

esp_err_t chassis_init(void);
void chassis_set_mode(chassis_mode_t mode);
chassis_mode_t chassis_get_mode(void);
void chassis_set_velocity(float vx, float vy, float omega);
void chassis_lift_jog(float steps_per_sec);
esp_err_t chassis_lift_move_to(int32_t target_position, float peak_sps);
bool chassis_lift_move_active(void);
bool chassis_lift_idle(void);
void chassis_set_lift_limits(int32_t min_position, int32_t max_position);
void chassis_invalidate_lift_limits(void);

/** 开环短距平移：dx 向右、dy 向前，单位 mm；四轮按距离比例分配峰值步频。 */
esp_err_t chassis_start_translation(float dx_mm, float dy_mm, float peak_sps);
bool chassis_translation_active(void);
bool chassis_translation_done(void);

void chassis_set_speeds(float chassis_sps, float lift_sps);
void chassis_get_speeds(float *chassis_sps, float *lift_sps);
void chassis_stop(void);
void chassis_get_status(chassis_status_t *out);

#ifdef __cplusplus
}
#endif
