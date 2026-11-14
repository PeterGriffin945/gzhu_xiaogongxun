/**
 * @file mecanum.h
 * @brief O 型麦克纳姆轮逆运动学
 *
 * 坐标约定：
 *   vy > 0 前进，vx > 0 右移，omega > 0 逆时针自转 (CCW, 俯视)
 * 轮序：RR(右后) / RF(右前) / LF(左前) / LR(左后)，与电机 1~4 对应
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float vx;       /* 右向线速度 m/s */
    float vy;       /* 前向线速度 m/s */
    float omega;    /* 自转角速度 rad/s，逆时针为正 */
} chassis_vel_t;

/**
 * @brief 逆运动学解算
 * @param[in]  cmd        底盘速度指令
 * @param[out] wheel_mps  四个轮子的线速度 m/s，顺序 RR/RF/LF/LR
 */
void mecanum_inverse(const chassis_vel_t *cmd, float wheel_mps[4]);

/** 轮线速度 (m/s) ↔ 步进频率 (步/秒) */
int32_t mecanum_mps_to_steps(float mps);
float mecanum_steps_to_mps(int32_t steps);

/** 单个轮子允许的最大线速度 (m/s)，由 STEPPER_MAX_SPEED 换算 */
float mecanum_max_wheel_mps(void);

#ifdef __cplusplus
}
#endif
