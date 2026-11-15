/**
 * @file line_follow.c
 * @brief 8 路模拟量加权 PID 循迹实现
 */
#include "line_follow.h"

#include <math.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "esp_timer.h"

#include "app_config.h"

#define WEIGHT_DEADBAND   0.05f
#define LOST_WEIGHT_SUM   0.10f
/* 转向角速度变化率上限（rad/s²）：防止 PID 瞬间打满方向导致过冲丢线 */
#define OMEGA_SLEW_RATE   6.0f

static const float s_position[LINE_SENSOR_COUNT] = {
    -3.5f, -2.5f, -1.5f, -0.5f, 0.5f, 1.5f, 2.5f, 3.5f
};

static portMUX_TYPE s_param_mux = portMUX_INITIALIZER_UNLOCKED;
static line_params_t s_params;

static float s_integral;
static float s_last_error;
static float s_last_omega;
static volatile float s_last_error_out;
static int64_t s_lost_since_us;

void line_follow_init(void)
{
    s_params.kp = 0.60f;
    s_params.ki = 0.00f;
    s_params.kd = 0.15f;
    s_params.threshold = 4.00f;      /* 0 ~ 5.0V 全量程可调 */
    s_params.black_high = false;     /* 默认：黑线电压 < 阈值 */
    s_params.reverse = false;
    s_params.base_speed = 0.30f;
    s_params.max_omega = 1.5f;
    line_follow_reset();
}

void line_follow_get_params(line_params_t *out)
{
    if (out == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_param_mux);
    *out = s_params;
    taskEXIT_CRITICAL(&s_param_mux);
}

void line_follow_set_params(const line_params_t *in)
{
    if (in == NULL) {
        return;
    }
    line_params_t p = *in;
    /* 防御性检查：调用方可能来自 HTTP/串口，NaN/Inf 会污染整个控制链。 */
    if (!isfinite(p.kp) || !isfinite(p.ki) || !isfinite(p.kd) ||
        !isfinite(p.threshold) || !isfinite(p.base_speed) ||
        !isfinite(p.max_omega)) {
        return;
    }
    /* 阈值全量程 0.0 ~ 5.0V：ADS1115 保持 ±6.144V 量程，不做 3.3V 截断 */
    if (p.threshold < 0.0f) {
        p.threshold = 0.0f;
    } else if (p.threshold > 5.0f) {
        p.threshold = 5.0f;
    }
    if (p.base_speed < 0.0f) {
        p.base_speed = 0.0f;
    } else if (p.base_speed > CHASSIS_MAX_LINEAR_V) {
        p.base_speed = CHASSIS_MAX_LINEAR_V;
    }
    if (p.max_omega < 0.0f) {
        p.max_omega = 0.0f;
    } else if (p.max_omega > CHASSIS_MAX_OMEGA) {
        p.max_omega = CHASSIS_MAX_OMEGA;
    }
    taskENTER_CRITICAL(&s_param_mux);
    s_params = p;
    taskEXIT_CRITICAL(&s_param_mux);
}

bool line_follow_is_black(float voltage)
{
    line_params_t p;
    line_follow_get_params(&p);
    return p.black_high ? (voltage > p.threshold) : (voltage < p.threshold);
}

void line_follow_reset(void)
{
    s_integral = 0.0f;
    s_last_error = 0.0f;
    s_last_omega = 0.0f;
    s_last_error_out = 0.0f;
    s_lost_since_us = 0;
}

float line_follow_update(const float v[8], float dt, bool *line_seen, float *error_out)
{
    line_params_t p;
    line_follow_get_params(&p);

    if (dt <= 0.0f) {
        dt = 0.01f;
    } else if (dt > 0.1f) {
        dt = 0.1f;
    }

    /* 1. 归一化权重：只有明显处于黑线电平的探头才参与加权。
       权重跨度不能直接取阈值（4V 阈值会把正常压线权重压到很小，
       导致界面显示黑线但 PID 认为“丢线”）；按阈值 1/4 且至少 0.5V。 */
    float sum_w = 0.0f;
    float sum_wp = 0.0f;
    bool any_black = false;
    const float span = fmaxf(0.5f, p.threshold * 0.25f);

    for (int i = 0; i < LINE_SENSOR_COUNT; ++i) {
        bool black = p.black_high ? (v[i] > p.threshold) : (v[i] < p.threshold);
        if (black) {
            any_black = true;
        }
        float d = p.black_high ? (v[i] - p.threshold) : (p.threshold - v[i]);
        float w = d / span;
        if (w < 0.0f) {
            w = 0.0f;
        } else if (w > 1.0f) {
            w = 1.0f;
        }
        if (w < WEIGHT_DEADBAND) {
            continue;
        }
        sum_w += w;
        sum_wp += w * (p.reverse ? -s_position[i] : s_position[i]);
    }

    /* 2. 丢线处理：不减速、不停车，保持上一次转向继续前进，直到重新压线。
         这样赛道有断线/反光时车辆不会突然停住。 */
    if (sum_w < LOST_WEIGHT_SUM) {
        int64_t now = esp_timer_get_time();
        if (s_lost_since_us == 0) {
            s_lost_since_us = now;
        }
        if (line_seen != NULL) {
            /* 与页面 8 路黑白判定保持一致：只要有一路判黑就算“看到线”，
               不能因为权重低于死区就显示丢线。 */
            *line_seen = any_black;
        }
        if (error_out != NULL) {
            *error_out = s_last_error;
        }
        return s_last_omega;
    }
    s_lost_since_us = 0;

    /* 3. 加权平均误差：微分必须使用更新前的上一次误差。 */
    float error = sum_wp / sum_w;
    float derivative = (error - s_last_error) / dt;
    s_last_error = error;
    s_last_error_out = error;
    if (error_out != NULL) {
        *error_out = error;
    }
    if (line_seen != NULL) {
        *line_seen = true;
    }

    /* 4. PID（积分限幅抗饱和） */
    s_integral += error * dt;
    const float int_limit = 5.0f;
    if (s_integral > int_limit) {
        s_integral = int_limit;
    } else if (s_integral < -int_limit) {
        s_integral = -int_limit;
    }

    float out = p.kp * error + p.ki * s_integral + p.kd * derivative;

    if (out > p.max_omega) {
        out = p.max_omega;
    } else if (out < -p.max_omega) {
        out = -p.max_omega;
    }

    /* 黑线偏右 (error>0) → 需要顺时针 → ω 为负 */
    float target = -out;

    /* 转向斜率限制：即使 Kp 很大，ω 也只能在 OMEGA_SLEW_RATE 内变化，
       避免“一看到线就猛打方向”造成过冲、来回摆甚至丢线。 */
    float max_delta = OMEGA_SLEW_RATE * dt;
    if (target > s_last_omega + max_delta) {
        target = s_last_omega + max_delta;
    } else if (target < s_last_omega - max_delta) {
        target = s_last_omega - max_delta;
    }
    s_last_omega = target;
    return s_last_omega;
}

float line_follow_get_error(void)
{
    return s_last_error_out;
}
