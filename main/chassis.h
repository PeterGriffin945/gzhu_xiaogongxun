/**
 * @file chassis.h
 * @brief 底盘控制：手动遥控 / 循迹双模式 + 麦轮解算 + 安全看门狗
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CHASSIS_MODE_MANUAL = 0,   /* Web 遥控 */
    CHASSIS_MODE_LINE   = 1,   /* 8 路 PID 循迹 */
} chassis_mode_t;

typedef struct {
    chassis_mode_t mode;
    float vx, vy, omega;           /* 当前生效速度指令 */
    float wheel_mps[4];            /* RR/RF/LF/LR 轮速 m/s */
    int32_t wheel_steps[4];        /* RR/RF/LF/LR 步频 */
    float line_error;
    bool line_seen;
    float lift_sps;
    uint32_t sensor_seq;
} chassis_status_t;

esp_err_t chassis_init(void);

void chassis_set_mode(chassis_mode_t mode);
chassis_mode_t chassis_get_mode(void);

/** 手动速度指令，必须在 MANUAL_CMD_TIMEOUT_MS 内持续刷新，否则自动停车 */
void chassis_set_velocity(float vx, float vy, float omega);

/** 升降点动速度 (步/秒)，正数上升；同样带 500ms 看门狗 */
void chassis_lift_jog(float steps_per_sec);

/** 平滑停车：底盘与升降目标清零 */
void chassis_stop(void);

void chassis_get_status(chassis_status_t *out);

#ifdef __cplusplus
}
#endif
