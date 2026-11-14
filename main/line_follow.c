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

#define LOST_GRACE_US     150000      /* 丢线后保持上次转向的时间 */
#define WEIGHT_DEADBAND   0.05f
#define LOST_WEIGHT_SUM   0.10f

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
    s_params.kp = 0.90f;
    s_params.ki = 0.00f;
    s_params.kd = 0.00f;
    s_params.threshold = 1.65f;
    s_params.black_high = false;     /* 默认：黑线电压 < 阈值 */
    s_params.reverse = false;
    s_params.base_speed = 0.30f;
    s_params.max_omega = 3.0f;
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
    if (p.threshold < 0.05f) {
        p.threshold = 0.05f;
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

    /* 1. 归一化权重：只有明显处于黑线电平的探头才参与加权 */
    float sum_w = 0.0f;
    float sum_wp = 0.0f;
    const float span = (p.threshold > 0.2f) ? p.threshold : 0.2f;

    for (int i = 0; i < LINE_SENSOR_COUNT; ++i) {
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

    /* 2. 丢线处理：短时间内保持上一次转向，超时停车 */
    if (sum_w < LOST_WEIGHT_SUM) {
        int64_t now = esp_timer_get_time();
        if (s_lost_since_us == 0) {
            s_lost_since_us = now;
        }
        if (line_seen != NULL) {
            *line_seen = (now - s_lost_since_us) < LOST_GRACE_US;
        }
        if (error_out != NULL) {
            *error_out = s_last_error;
        }
        if ((now - s_lost_since_us) >= LOST_GRACE_US) {
            s_integral = 0.0f;
            s_last_omega = 0.0f;
            return 0.0f;
        }
        return s_last_omega;
    }
    s_lost_since_us = 0;

    /* 3. 加权平均误差 */
    float error = sum_wp / sum_w;
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

    float derivative = (error - s_last_error) / dt;
    float out = p.kp * error + p.ki * s_integral + p.kd * derivative;

    if (out > p.max_omega) {
        out = p.max_omega;
    } else if (out < -p.max_omega) {
        out = -p.max_omega;
    }

    /* 黑线偏右 (error>0) → 需要顺时针 → ω 为负 */
    s_last_omega = -out;
    return s_last_omega;
}

float line_follow_get_error(void)
{
    return s_last_error_out;
}
