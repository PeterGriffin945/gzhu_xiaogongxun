/**
 * @file line_follow.h
 * @brief 8 路模拟量加权 PID 循迹
 *
 * 探头 1..8 从左到右排列，位置权重 -3.5 .. +3.5。
 * 误差 > 0 表示黑线偏右，控制器输出 ω < 0（顺时针修正）。
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float kp;
    float ki;
    float kd;
    float threshold;      /* 黑白判定电压 V */
    bool  black_high;     /* true = 黑线电压高于阈值 */
    bool  reverse;        /* true = 探头实际安装为 8→1，位置权重取反 */
    float base_speed;     /* 循迹基础前进速度 m/s */
    float max_omega;      /* 修正角速度上限 rad/s */
} line_params_t;

void line_follow_init(void);

void line_follow_get_params(line_params_t *out);
void line_follow_set_params(const line_params_t *in);

/** 依据当前阈值/极性判断某路是否为黑线 */
bool line_follow_is_black(float voltage);

/** 切换到循迹前重置积分项与丢线计时 */
void line_follow_reset(void);

/**
 * @brief 计算循迹输出
 * @param[in]  v         8 路电压，v[0]=探头1
 * @param[in]  dt        采样间隔 s
 * @param[out] line_seen 是否稳定看到黑线（丢线 150ms 内仍为 true）
 * @return 期望角速度 rad/s
 */
float line_follow_update(const float v[8], float dt, bool *line_seen, float *error_out);

/** 最近一次误差（调试用） */
float line_follow_get_error(void);

#ifdef __cplusplus
}
#endif
