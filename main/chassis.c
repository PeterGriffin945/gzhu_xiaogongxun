/**
 * @file chassis.c
 * @brief 底盘 100Hz 控制循环与全部运动命令仲裁
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

#define CONTROL_PERIOD_MS 10
#define SENSOR_STALE_US 120000

static const char *TAG = "chassis";
static volatile chassis_mode_t s_mode = CHASSIS_MODE_MANUAL;
static volatile float s_cmd_vx;
static volatile float s_cmd_vy;
static volatile float s_cmd_omega;
static volatile int64_t s_cmd_us;
static volatile float s_lift_cmd;
static volatile int64_t s_lift_cmd_us;
static volatile float s_base_sps = CHASSIS_BASE_SPEED_DEFAULT;
static volatile float s_lift_sps = LIFT_SPEED_DEFAULT;
static SemaphoreHandle_t s_status_mutex;
static chassis_status_t s_status;
static uint8_t s_translation_mask;
static volatile uint32_t s_control_generation;
static volatile bool s_lift_limits_valid;
static volatile int32_t s_lift_min_position;
static volatile int32_t s_lift_max_position;
static portMUX_TYPE s_control_mux = portMUX_INITIALIZER_UNLOCKED;

static float clampf(float value, float low, float high)
{
    return (value < low) ? low : ((value > high) ? high : value);
}

static void normalize_peak(float steps[4], float target_sps)
{
    float peak = 0.0f;
    for (int i = 0; i < 4; ++i) {
        peak = fmaxf(peak, fabsf(steps[i]));
    }
    if (peak < 1.0f || target_sps < 1.0f) {
        return;
    }
    float scale = target_sps / peak;
    for (int i = 0; i < 4; ++i) {
        steps[i] *= scale;
    }
}

static uint32_t step_magnitude(int32_t steps)
{
    return (steps >= 0) ? (uint32_t)steps : (uint32_t)(-(int64_t)steps);
}

/* 调用方必须持有 s_control_mux，锁顺序始终为 control -> move。 */
static void cancel_translation_locked(void)
{
    for (int i = 0; i < 4; ++i) {
        if (s_translation_mask & (1U << i)) {
            stepper_stop((stepper_axis_t)i);
        }
    }
    s_translation_mask = 0;
}

static bool translation_active_locked(void)
{
    if (s_translation_mask == 0) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if ((s_translation_mask & (1U << i)) && stepper_move_active((stepper_axis_t)i)) {
            return true;
        }
    }
    return false;
}

static bool lift_idle_locked(void)
{
    return s_lift_cmd == 0.0f &&
           !stepper_move_active(AXIS_LIFT) &&
           stepper_get_target(AXIS_LIFT) == 0.0f &&
           stepper_get_speed(AXIS_LIFT) == 0.0f;
}

static bool lift_jog_within_limits_locked(float command, int32_t position)
{
    if (!s_lift_limits_valid) {
        return true;
    }
    return !((command > 0.0f && position >= s_lift_max_position) ||
             (command < 0.0f && position <= s_lift_min_position));
}

void chassis_set_mode(chassis_mode_t mode)
{
    taskENTER_CRITICAL(&s_control_mux);
    if (mode == s_mode) {
        taskEXIT_CRITICAL(&s_control_mux);
        return;
    }
    s_control_generation++;
    cancel_translation_locked();
    s_cmd_vx = 0.0f;
    s_cmd_vy = 0.0f;
    s_cmd_omega = 0.0f;
    s_cmd_us = esp_timer_get_time();
    if (mode == CHASSIS_MODE_LINE) {
        line_follow_reset();
    }
    if (mode == CHASSIS_MODE_POSITION) {
        /* 位置模式由 chassis_start_translation() 接管轮速；若外部直接切进来，
           必须显式清零，否则会沿用上一次手动速度继续跑。 */
        const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        stepper_set_chassis_speeds(zero);
    }
    s_mode = mode;
    taskEXIT_CRITICAL(&s_control_mux);
}

chassis_mode_t chassis_get_mode(void)
{
    return s_mode;
}

void chassis_set_velocity(float vx, float vy, float omega)
{
    if (!isfinite(vx) || !isfinite(vy) || !isfinite(omega)) {
        return;
    }
    taskENTER_CRITICAL(&s_control_mux);
    if (s_mode == CHASSIS_MODE_POSITION) {
        taskEXIT_CRITICAL(&s_control_mux);
        return;
    }
    s_control_generation++;
    s_cmd_vx = clampf(vx, -CHASSIS_MAX_LINEAR_V, CHASSIS_MAX_LINEAR_V);
    s_cmd_vy = clampf(vy, -CHASSIS_MAX_LINEAR_V, CHASSIS_MAX_LINEAR_V);
    s_cmd_omega = clampf(omega, -CHASSIS_MAX_OMEGA, CHASSIS_MAX_OMEGA);
    s_cmd_us = esp_timer_get_time();
    taskEXIT_CRITICAL(&s_control_mux);
}

void chassis_lift_jog(float steps_per_sec)
{
    if (!isfinite(steps_per_sec)) {
        return;
    }
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_control_mux);
    if (stepper_move_active(AXIS_LIFT)) {
        taskEXIT_CRITICAL(&s_control_mux);
        return;
    }
    float command = (steps_per_sec > 0.0f) ? 1.0f : ((steps_per_sec < 0.0f) ? -1.0f : 0.0f);
    if (!lift_jog_within_limits_locked(command, stepper_get_position(AXIS_LIFT))) {
        command = 0.0f;
    }
    s_control_generation++;
    s_lift_cmd = command;
    s_lift_cmd_us = now;
    taskEXIT_CRITICAL(&s_control_mux);
}

esp_err_t chassis_lift_move_to(int32_t target_position, float peak_sps)
{
    if (!isfinite(peak_sps) || peak_sps <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_OK;
    taskENTER_CRITICAL(&s_control_mux);
    if (s_lift_limits_valid &&
        (target_position < s_lift_min_position || target_position > s_lift_max_position)) {
        result = ESP_ERR_INVALID_ARG;
    } else {
        int64_t delta = (int64_t)target_position - (int64_t)stepper_get_position(AXIS_LIFT);
        if (delta == 0) {
            if (lift_idle_locked()) {
                s_control_generation++;
            } else {
                result = ESP_ERR_INVALID_STATE;
            }
        } else if (delta < INT32_MIN || delta > INT32_MAX) {
            result = ESP_ERR_INVALID_ARG;
        } else {
            s_control_generation++;
            s_lift_cmd = 0.0f;
            s_lift_cmd_us = esp_timer_get_time();
            result = stepper_start_move(AXIS_LIFT, (int32_t)delta, peak_sps);
        }
    }
    taskEXIT_CRITICAL(&s_control_mux);
    return result;
}

bool chassis_lift_move_active(void)
{
    return stepper_move_active(AXIS_LIFT);
}

bool chassis_lift_idle(void)
{
    taskENTER_CRITICAL(&s_control_mux);
    bool idle = lift_idle_locked();
    taskEXIT_CRITICAL(&s_control_mux);
    return idle;
}

void chassis_set_lift_limits(int32_t min_position, int32_t max_position)
{
    taskENTER_CRITICAL(&s_control_mux);
    s_control_generation++;
    s_lift_limits_valid = (min_position <= max_position);
    if (s_lift_limits_valid) {
        s_lift_min_position = min_position;
        s_lift_max_position = max_position;
        if (!lift_jog_within_limits_locked(s_lift_cmd, stepper_get_position(AXIS_LIFT))) {
            s_lift_cmd = 0.0f;
            s_lift_cmd_us = esp_timer_get_time();
        }
    }
    taskEXIT_CRITICAL(&s_control_mux);
}

void chassis_invalidate_lift_limits(void)
{
    taskENTER_CRITICAL(&s_control_mux);
    s_control_generation++;
    s_lift_limits_valid = false;
    taskEXIT_CRITICAL(&s_control_mux);
}

esp_err_t chassis_start_translation(float dx_mm, float dy_mm, float peak_sps)
{
    if (!isfinite(dx_mm) || !isfinite(dy_mm) || !isfinite(peak_sps)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (peak_sps <= 0.0f) {
        return ESP_ERR_INVALID_STATE;
    }
    if (fabsf(dx_mm) < 0.01f && fabsf(dy_mm) < 0.01f) {
        return ESP_ERR_INVALID_ARG;
    }

    chassis_vel_t delta = {
        .vx = dx_mm * 0.001f,
        .vy = dy_mm * 0.001f,
        .omega = 0.0f,
    };
    float wheel_distance[4] = { 0 };
    int32_t wheel_steps[4] = { 0 };
    uint32_t max_steps = 0;
    uint8_t mask = 0;
    mecanum_inverse(&delta, wheel_distance);
    for (int i = 0; i < 4; ++i) {
        wheel_steps[i] = mecanum_mps_to_steps(wheel_distance[i]);
        uint32_t magnitude = step_magnitude(wheel_steps[i]);
        if (magnitude > 0) {
            mask |= (1U << i);
            max_steps = (magnitude > max_steps) ? magnitude : max_steps;
        }
    }
    if (mask == 0 || max_steps == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_OK;
    taskENTER_CRITICAL(&s_control_mux);
    if (translation_active_locked() || !stepper_chassis_enabled()) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        for (int i = 0; i < 4; ++i) {
            if (fabsf(stepper_get_speed((stepper_axis_t)i)) > 1.0f) {
                result = ESP_ERR_INVALID_STATE;
                break;
            }
        }
    }

    if (result == ESP_OK) {
        s_control_generation++;
        cancel_translation_locked();
        s_mode = CHASSIS_MODE_POSITION;
        s_translation_mask = mask;
        for (int i = 0; i < 4; ++i) {
            if (!(mask & (1U << i))) {
                continue;
            }
            uint32_t magnitude = step_magnitude(wheel_steps[i]);
            float wheel_peak = peak_sps * (float)magnitude / (float)max_steps;
            result = stepper_start_move((stepper_axis_t)i, wheel_steps[i], wheel_peak);
            if (result != ESP_OK) {
                cancel_translation_locked();
                s_mode = CHASSIS_MODE_MANUAL;
                break;
            }
        }
    }
    taskEXIT_CRITICAL(&s_control_mux);
    return result;
}

bool chassis_translation_active(void)
{
    taskENTER_CRITICAL(&s_control_mux);
    bool active = translation_active_locked();
    taskEXIT_CRITICAL(&s_control_mux);
    return active;
}

bool chassis_translation_done(void)
{
    taskENTER_CRITICAL(&s_control_mux);
    if (s_translation_mask == 0 || translation_active_locked()) {
        taskEXIT_CRITICAL(&s_control_mux);
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if ((s_translation_mask & (1U << i)) &&
            stepper_get_move_status((stepper_axis_t)i) != STEPPER_MOVE_DONE) {
            taskEXIT_CRITICAL(&s_control_mux);
            return false;
        }
    }
    taskEXIT_CRITICAL(&s_control_mux);
    return true;
}

void chassis_set_speeds(float chassis_sps, float lift_sps)
{
    if (!isfinite(chassis_sps) || !isfinite(lift_sps)) {
        return;
    }
    taskENTER_CRITICAL(&s_control_mux);
    s_control_generation++;
    s_base_sps = clampf(chassis_sps, CHASSIS_BASE_SPEED_MIN, CHASSIS_BASE_SPEED_MAX);
    s_lift_sps = clampf(lift_sps, LIFT_SPEED_MIN, LIFT_SPEED_MAX);
    taskEXIT_CRITICAL(&s_control_mux);
    ESP_LOGI(TAG, "speeds chassis=%.0f lift=%.0f sps", s_base_sps, s_lift_sps);
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
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_control_mux);
    s_control_generation++;
    s_mode = CHASSIS_MODE_MANUAL;
    s_cmd_vx = 0.0f;
    s_cmd_vy = 0.0f;
    s_cmd_omega = 0.0f;
    s_cmd_us = now;
    s_lift_cmd = 0.0f;
    s_lift_cmd_us = now;
    s_translation_mask = 0;
    line_follow_reset();
    stepper_stop_all();
    taskEXIT_CRITICAL(&s_control_mux);
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
    float line_err = 0.0f;
    float line_omega = 0.0f;
    bool line_seen = false;
    uint32_t line_seq = 0;
    int64_t last_line_frame_us = 0;
    int64_t last_sensor_change_us = 0;

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
        int64_t now = esp_timer_get_time();
        chassis_vel_t vel = { 0 };
        float volts[LINE_SENSOR_COUNT] = { 0 };
        uint32_t seq = ads1115_get_seq();

        taskENTER_CRITICAL(&s_control_mux);
        uint32_t generation = s_control_generation;
        chassis_mode_t mode = s_mode;
        float cmd_vx = s_cmd_vx;
        float cmd_vy = s_cmd_vy;
        float cmd_omega = s_cmd_omega;
        int64_t cmd_us = s_cmd_us;
        float lift_cmd = s_lift_cmd;
        int64_t lift_cmd_us = s_lift_cmd_us;
        float base_sps = s_base_sps;
        float configured_lift_sps = s_lift_sps;
        taskEXIT_CRITICAL(&s_control_mux);

        if (mode == CHASSIS_MODE_LINE) {
            const float base_mps = mecanum_steps_to_mps((int32_t)lroundf(base_sps));
            if (ads1115_get_latest(volts, &seq)) {
                if (seq != line_seq) {
                    float dt = (last_line_frame_us == 0) ? 0.012f
                               : (float)(now - last_line_frame_us) * 1e-6f;
                    line_omega = line_follow_update(volts, dt, &line_seen, &line_err);
                    line_seq = seq;
                    last_line_frame_us = now;
                    last_sensor_change_us = now;
                } else if (now - last_sensor_change_us > SENSOR_STALE_US) {
                    /* 采样停滞只更新界面状态，保留上一次转向继续走 */
                    line_seen = false;
                }
            } else {
                line_seen = false;
            }
            /* 丢线不停车：基础航速始终有效，转向角沿用最近一次 PID 输出 */
            vel.vy = base_mps;
            vel.omega = line_omega;
        } else if (mode == CHASSIS_MODE_MANUAL) {
            if (now - cmd_us <= (int64_t)MANUAL_CMD_TIMEOUT_MS * 1000) {
                vel.vx = cmd_vx;
                vel.vy = cmd_vy;
                vel.omega = cmd_omega;
            }
            line_seen = false;
        }

        bool lift_move_active = stepper_move_active(AXIS_LIFT);
        bool lift_jog_expired = now - lift_cmd_us > (int64_t)LIFT_JOG_TIMEOUT_MS * 1000;
        float lift_sps = lift_move_active ? stepper_get_speed(AXIS_LIFT) : 0.0f;
        if (!lift_move_active && !lift_jog_expired) {
            lift_sps = lift_cmd * configured_lift_sps;
        }

        if (!stepper_chassis_enabled()) {
            vel.vx = 0.0f;
            vel.vy = 0.0f;
            vel.omega = 0.0f;
        }

        float wheel_mps[4] = { 0 };
        int32_t wheel_steps[4] = { 0 };
        float steps_f[4] = { 0 };
        if (mode != CHASSIS_MODE_POSITION) {
            mecanum_inverse(&vel, wheel_mps);
            for (int i = 0; i < 4; ++i) {
                steps_f[i] = (float)mecanum_mps_to_steps(wheel_mps[i]);
            }
            if (mode == CHASSIS_MODE_MANUAL) {
                normalize_peak(steps_f, base_sps);
            }
        }

        taskENTER_CRITICAL(&s_control_mux);
        if (s_control_generation == generation) {
            if (lift_move_active || stepper_move_active(AXIS_LIFT)) {
                lift_sps = stepper_get_speed(AXIS_LIFT);
            } else {
                if (lift_jog_expired ||
                    !lift_jog_within_limits_locked(lift_cmd, stepper_get_position(AXIS_LIFT))) {
                    s_lift_cmd = 0.0f;
                    lift_sps = 0.0f;
                }
                if (!stepper_lift_enabled()) {
                    lift_sps = 0.0f;
                }
                stepper_set_speed(AXIS_LIFT, lift_sps);
            }
        } else {
            lift_sps = stepper_get_speed(AXIS_LIFT);
        }
        taskEXIT_CRITICAL(&s_control_mux);

        taskENTER_CRITICAL(&s_control_mux);
        if (s_control_generation == generation && mode != CHASSIS_MODE_POSITION) {
            if (!stepper_chassis_enabled()) {
                for (int i = 0; i < 4; ++i) {
                    steps_f[i] = 0.0f;
                }
            }
            stepper_set_chassis_speeds(steps_f);
        } else {
            for (int i = 0; i < 4; ++i) {
                steps_f[i] = stepper_get_speed((stepper_axis_t)i);
            }
        }
        taskEXIT_CRITICAL(&s_control_mux);

        for (int i = 0; i < 4; ++i) {
            wheel_steps[i] = (int32_t)lroundf(steps_f[i]);
        }
        publish_status(&vel, wheel_mps, wheel_steps, line_err, line_seen, lift_sps, seq);
    }
}

esp_err_t chassis_init(void)
{
    s_status_mutex = xSemaphoreCreateMutex();
    if (s_status_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t ret = xTaskCreatePinnedToCore(chassis_task, "chassis", 4096, NULL, 6, NULL, 1);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
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
