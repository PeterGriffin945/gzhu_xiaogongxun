/**
 * @file chassis.c
 * @brief 底盘 100Hz 控制循环
 */
#include "chassis.h"

#include <math.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ads1115.h"
#include "app_config.h"
#include "line_follow.h"
#include "mecanum.h"
#include "stepper.h"

static const char *TAG = "chassis";

#define CONTROL_PERIOD_MS 10

static volatile chassis_mode_t s_mode = CHASSIS_MODE_MANUAL;
static volatile float s_cmd_vx;
static volatile float s_cmd_vy;
static volatile float s_cmd_omega;
static volatile int64_t s_cmd_us;
static volatile float s_lift_cmd;
static volatile int64_t s_lift_cmd_us;

/* 运行时可调基础转速（步/秒） */
static volatile float s_base_sps = CHASSIS_BASE_SPEED_DEFAULT;
static volatile float s_lift_sps = LIFT_SPEED_DEFAULT;

static SemaphoreHandle_t s_status_mutex;
static chassis_status_t s_status;

static float clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

/**
 * @brief 把四轮步频向量的峰值缩放到 target_sps
 *
 * 只改变整体快慢，不改变各轮的比例，因此平移/斜向/自转的方向不变。
 * 全零输入直接返回，避免除零。
 */
static void normalize_peak(float steps[4], float target_sps)
{
    float peak = 0.0f;
    for (int i = 0; i < 4; ++i) {
        float a = fabsf(steps[i]);
        if (a > peak) {
            peak = a;
        }
    }
    if (peak < 1.0f || target_sps < 1.0f) {
        return;
    }
    const float scale = target_sps / peak;
    for (int i = 0; i < 4; ++i) {
        steps[i] *= scale;
    }
}

void chassis_set_mode(chassis_mode_t mode)
{
    if (mode == s_mode) {
        return;
    }
    /* 切模式先停，避免残留速度 */
    s_cmd_vx = 0.0f;
    s_cmd_vy = 0.0f;
    s_cmd_omega = 0.0f;
    s_cmd_us = esp_timer_get_time();
    if (mode == CHASSIS_MODE_LINE) {
        line_follow_reset();
    }
    s_mode = mode;
    ESP_LOGI(TAG, "mode -> %s", (mode == CHASSIS_MODE_LINE) ? "LINE" : "MANUAL");
}

chassis_mode_t chassis_get_mode(void)
{
    return s_mode;
}

void chassis_set_velocity(float vx, float vy, float omega)
{
    s_cmd_vx = clampf(vx, -CHASSIS_MAX_LINEAR_V, CHASSIS_MAX_LINEAR_V);
    s_cmd_vy = clampf(vy, -CHASSIS_MAX_LINEAR_V, CHASSIS_MAX_LINEAR_V);
    s_cmd_omega = clampf(omega, -CHASSIS_MAX_OMEGA, CHASSIS_MAX_OMEGA);
    s_cmd_us = esp_timer_get_time();
}

void chassis_lift_jog(float steps_per_sec)
{
    /* 只保留方向，具体速度取运行时 lift_speed */
    s_lift_cmd = (steps_per_sec > 0.0f) ? 1.0f
               : (steps_per_sec < 0.0f) ? -1.0f
                                        : 0.0f;
    s_lift_cmd_us = esp_timer_get_time();
}

void chassis_set_speeds(float chassis_sps, float lift_sps)
{
    chassis_sps = clampf(chassis_sps, CHASSIS_BASE_SPEED_MIN, CHASSIS_BASE_SPEED_MAX);
    lift_sps = clampf(lift_sps, LIFT_SPEED_MIN, LIFT_SPEED_MAX);
    s_base_sps = chassis_sps;
    s_lift_sps = lift_sps;
    ESP_LOGI(TAG, "speeds -> chassis %.0f sps / lift %.0f sps", chassis_sps, lift_sps);
}

void chassis_get_speeds(float *chassis_sps, float *lift_sps)
{
    if (chassis_sps != NULL) {
        *chassis_sps = s_base_sps;
    }
    if (lift_sps != NULL) {
        *lift_sps = s_lift_sps;
    }
}

void chassis_stop(void)
{
    s_mode = CHASSIS_MODE_MANUAL;
    s_cmd_vx = 0.0f;
    s_cmd_vy = 0.0f;
    s_cmd_omega = 0.0f;
    s_cmd_us = esp_timer_get_time();
    s_lift_cmd = 0.0f;
    s_lift_cmd_us = esp_timer_get_time();
    line_follow_reset();
    stepper_stop_all();
}

static void publish_status(const chassis_vel_t *vel, const float wheel_mps[4],
                           const int32_t wheel_steps[4], float line_err,
                           bool line_seen, float lift_sps, uint32_t seq)
{
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }
    s_status.mode = s_mode;
    s_status.vx = vel->vx;
    s_status.vy = vel->vy;
    s_status.omega = vel->omega;
    for (int i = 0; i < 4; ++i) {
        s_status.wheel_mps[i] = wheel_mps[i];
        s_status.wheel_steps[i] = wheel_steps[i];
    }
    s_status.line_error = line_err;
    s_status.line_seen = line_seen;
    s_status.lift_sps = lift_sps;
    s_status.sensor_seq = seq;
    xSemaphoreGive(s_status_mutex);
}

static void chassis_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    int64_t last_us = esp_timer_get_time();

    float line_err = 0.0f;
    bool line_seen = false;

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));

        int64_t now = esp_timer_get_time();
        float dt = (float)(now - last_us) * 1e-6f;
        last_us = now;
        if (dt <= 0.0f) {
            dt = 0.01f;
        } else if (dt > 0.1f) {
            dt = 0.1f;
        }

        chassis_vel_t vel = { 0 };
        uint32_t seq = ads1115_get_seq();

        if (s_mode == CHASSIS_MODE_LINE) {
            float volts[LINE_SENSOR_COUNT] = { 0 };
            /* 循迹基础航速 = 运行时 chassis_base_speed（步/秒）折算成 m/s */
            const float base_mps =
                mecanum_steps_to_mps((int32_t)lroundf(s_base_sps));

            if (ads1115_get_latest(volts, &seq)) {
                float omega = line_follow_update(volts, dt, &line_seen, &line_err);
                vel.vy = line_seen ? base_mps : 0.0f;
                vel.vx = 0.0f;
                vel.omega = line_seen ? omega : 0.0f;
            } else {
                line_seen = false;
            }
        } else {
            /* 手动模式：指令超时自动归零 */
            if ((now - s_cmd_us) > (int64_t)MANUAL_CMD_TIMEOUT_MS * 1000) {
                vel.vx = 0.0f;
                vel.vy = 0.0f;
                vel.omega = 0.0f;
            } else {
                vel.vx = s_cmd_vx;
                vel.vy = s_cmd_vy;
                vel.omega = s_cmd_omega;
            }
            line_seen = false;
        }

        /* 升降点动看门狗：方向 × 运行时 lift_speed */
        float lift_sps = 0.0f;
        if ((now - s_lift_cmd_us) <= (int64_t)LIFT_JOG_TIMEOUT_MS * 1000) {
            lift_sps = s_lift_cmd * s_lift_sps;
        } else {
            s_lift_cmd = 0.0f;
        }
        if (!stepper_lift_enabled()) {
            lift_sps = 0.0f;
        }
        stepper_set_speed(AXIS_LIFT, lift_sps);

        /* EN 关闭时底盘目标强制清零 */
        if (!stepper_chassis_enabled()) {
            vel.vx = 0.0f;
            vel.vy = 0.0f;
            vel.omega = 0.0f;
        }

        float wheel_mps[4] = { 0 };
        int32_t wheel_steps[4] = { 0 };
        mecanum_inverse(&vel, wheel_mps);
        float steps_f[4];
        for (int i = 0; i < 4; ++i) {
            steps_f[i] = (float)mecanum_mps_to_steps(wheel_mps[i]);
        }

        /* 手动遥控：方向由 vx/vy/ω 决定，幅值统一归一化到 chassis_base_speed，
           这样速度滑块才能实时决定实际快慢（斜向/自转按比例分配） */
        if (s_mode == CHASSIS_MODE_MANUAL) {
            normalize_peak(steps_f, s_base_sps);
        }

        for (int i = 0; i < 4; ++i) {
            wheel_steps[i] = (int32_t)lroundf(steps_f[i]);
        }
        stepper_set_chassis_speeds(steps_f);

        publish_status(&vel, wheel_mps, wheel_steps, line_err, line_seen,
                       lift_sps, seq);
    }
}

esp_err_t chassis_init(void)
{
    s_status_mutex = xSemaphoreCreateMutex();
    if (s_status_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_cmd_us = 0;
    s_lift_cmd_us = 0;

    BaseType_t ret = xTaskCreatePinnedToCore(chassis_task, "chassis", 4096,
                                             NULL, 6, NULL, 1);
    if (ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "100Hz control loop started (wheel D=%.1fmm, k=%.3fm)",
             WHEEL_DIAMETER_MM, ((WHEELBASE_MM + TRACK_MM) * 0.5f) * 0.001f);
    return ESP_OK;
}

void chassis_get_status(chassis_status_t *out)
{
    if (out == NULL || s_status_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        *out = s_status;
        xSemaphoreGive(s_status_mutex);
    }
}
