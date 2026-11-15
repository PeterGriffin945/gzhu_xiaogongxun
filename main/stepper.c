/**
 * @file stepper.c
 * @brief 5 轴步进电机 DDS 脉冲发生器与开环定步执行器
 */
#include "stepper.h"

#include <math.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"

#define POSITION_START_SPEED 40.0f

static const char *TAG = "stepper";

typedef struct {
    gpio_num_t step_pin;
    gpio_num_t dir_pin;
    bool dir_invert;
} axis_hw_t;

static const axis_hw_t s_hw[AXIS_COUNT] = {
    [AXIS_RR] = { PIN_M1_RR_STEP, PIN_M1_RR_DIR, false },
    [AXIS_RF] = { PIN_M2_RF_STEP, PIN_M2_RF_DIR, false },
    [AXIS_LF] = { PIN_M3_LF_STEP, PIN_M3_LF_DIR, true },
    [AXIS_LR] = { PIN_M4_LR_STEP, PIN_M4_LR_DIR, true },
    [AXIS_LIFT] = { PIN_M5_LIFT_STEP, PIN_M5_LIFT_DIR, false },
};

static volatile int32_t s_phase_inc[AXIS_COUNT];
static volatile float s_target[AXIS_COUNT];
static volatile float s_speed[AXIS_COUNT];
static volatile uint32_t s_phase[AXIS_COUNT];
static uint8_t s_pulse_hold[AXIS_COUNT];
static bool s_dir_invert[AXIS_COUNT];
static volatile bool s_en_chassis;
static volatile bool s_en_lift;
static volatile float s_accel = STEPPER_DEFAULT_ACCEL;
static gptimer_handle_t s_timer;

static volatile int32_t s_position[AXIS_COUNT];
static volatile int32_t s_move_remaining[AXIS_COUNT];
static volatile int8_t s_move_direction[AXIS_COUNT];
static volatile bool s_move_active[AXIS_COUNT];
static volatile bool s_move_stop_requested[AXIS_COUNT];
static volatile float s_move_peak[AXIS_COUNT];
static volatile uint32_t s_move_generation[AXIS_COUNT];
static volatile stepper_move_status_t s_move_status[AXIS_COUNT];
static portMUX_TYPE s_move_mux = portMUX_INITIALIZER_UNLOCKED;

/* 调用方必须持有 s_move_mux；活动定步的取消在此原子发布。 */
static void cancel_move_locked(stepper_axis_t axis)
{
    if (s_move_active[axis]) {
        s_move_generation[axis]++;
        s_move_remaining[axis] = 0;
        s_move_stop_requested[axis] = false;
        s_move_status[axis] = STEPPER_MOVE_CANCELLED;
        s_target[axis] = 0.0f;
        s_speed[axis] = 0.0f;
        s_phase_inc[axis] = 0;
        s_phase[axis] = 0;
        s_move_active[axis] = false;
    }
}

static bool IRAM_ATTR stepper_timer_isr(gptimer_handle_t timer,
                                         const gptimer_alarm_event_data_t *edata,
                                         void *user_ctx)
{
    (void)timer;
    (void)edata;
    (void)user_ctx;

    taskENTER_CRITICAL_ISR(&s_move_mux);
    for (int i = 0; i < AXIS_COUNT; ++i) {
        bool enabled = (i == AXIS_LIFT) ? s_en_lift : s_en_chassis;
        if (!enabled) {
            if (s_pulse_hold[i] != 0) {
                s_pulse_hold[i] = 0;
                gpio_set_level(s_hw[i].step_pin, 0);
            }
            continue;
        }

        bool fire = false;
        int32_t inc = s_phase_inc[i];
        if (inc != 0) {
            uint32_t prev = s_phase[i];
            uint32_t next = prev + (uint32_t)inc;
            s_phase[i] = next;
            fire = (inc > 0) ? (next < prev) : (next > prev);
        }

        bool was_high = (s_pulse_hold[i] != 0);
        if (was_high) {
            s_pulse_hold[i] = 0;
            gpio_set_level(s_hw[i].step_pin, 0);
        }

        if (fire && !was_high) {
            gpio_set_level(s_hw[i].step_pin, 1);
            s_pulse_hold[i] = 1;

            if (s_move_active[i]) {
                int8_t direction = s_move_direction[i];
                s_position[i] += direction;
                int32_t remaining = s_move_remaining[i] - 1;
                s_move_remaining[i] = remaining;
                if (remaining <= 0) {
                    /* 最后一步由 ISR 自己封口，避免 1ms 斜坡任务再次补发脉冲。 */
                    s_move_generation[i]++;
                    s_move_stop_requested[i] = false;
                    s_move_status[i] = STEPPER_MOVE_DONE;
                    s_target[i] = 0.0f;
                    s_speed[i] = 0.0f;
                    s_phase_inc[i] = 0;
                    s_phase[i] = 0;
                    s_move_active[i] = false;
                }
            } else if (inc != 0) {
                s_position[i] += (inc > 0) ? 1 : -1;
            }
        }
    }
    taskEXIT_CRITICAL_ISR(&s_move_mux);
    return false;
}

static void apply_dir(stepper_axis_t axis, bool forward)
{
    int level = forward ? 1 : 0;
    if (s_dir_invert[axis]) {
        level = !level;
    }
    gpio_set_level(s_hw[axis].dir_pin, level);
}

static int32_t speed_to_phase_inc(float sps)
{
    const double scale = 4294967296.0 / (double)STEPPER_TICK_HZ;
    double inc = (double)sps * scale;
    if (inc > (double)INT32_MAX) {
        inc = (double)INT32_MAX;
    } else if (inc < (double)INT32_MIN) {
        inc = (double)INT32_MIN;
    }
    return (int32_t)llround(inc);
}

static float clamp_speed(float sps)
{
    if (sps > STEPPER_MAX_SPEED) {
        return STEPPER_MAX_SPEED;
    }
    if (sps < -STEPPER_MAX_SPEED) {
        return -STEPPER_MAX_SPEED;
    }
    return sps;
}

static float approach_speed(float current, float target, float step)
{
    if (current < target) {
        return fminf(current + step, target);
    }
    return fmaxf(current - step, target);
}

/* 软加速系数：速度接近 0 或接近目标时降低加速度，形成 S 形曲线。
 * 全步模式下这能明显减小起步/到位的冲击、振动和“咔哒”噪声。 */
static float accel_scale(float current, float target)
{
    float magnitude = fmaxf(fabsf(current), fabsf(target));
    if (magnitude < 1.0f) {
        return 1.0f;
    }
    float ratio = fabsf(current) / magnitude;
    if (ratio > 1.0f) {
        ratio = 1.0f;
    }
    float bell = 4.0f * ratio * (1.0f - ratio);   /* 两端 0、中间 1 */
    return 0.25f + 0.75f * bell;
}

static float approach_speed_smooth(float current, float target, float step)
{
    return approach_speed(current, target, step * accel_scale(current, target));
}

static void stepper_ramp_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    int64_t last_us = esp_timer_get_time();

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(STEPPER_RAMP_PERIOD_MS));
        int64_t now_us = esp_timer_get_time();
        float dt = (float)(now_us - last_us) * 1e-6f;
        last_us = now_us;
        if (dt <= 0.0f) {
            dt = 0.001f;
        } else if (dt > 0.05f) {
            dt = 0.05f;
        }
        float acc_step = s_accel * dt;

        for (int i = 0; i < AXIS_COUNT; ++i) {
            taskENTER_CRITICAL(&s_move_mux);
            uint32_t generation = s_move_generation[i];
            bool move_active = s_move_active[i];
            bool stop_requested = s_move_stop_requested[i];
            int32_t remaining = s_move_remaining[i];
            int direction = s_move_direction[i];
            float move_peak = s_move_peak[i];
            float cur = s_speed[i];
            float target = s_target[i];
            taskEXIT_CRITICAL(&s_move_mux);

            if (move_active) {
                if (remaining <= 0 || direction == 0) {
                    continue;
                }

                float desired = 0.0f;
                bool set_direction = false;
                if (stop_requested) {
                    cur = approach_speed(cur, 0.0f, acc_step);
                } else {
                    /* 剩余脉冲越少，允许速度越低，形成短距离也能使用的三角形/梯形曲线。 */
                    float limit = sqrtf(2.0f * s_accel * (float)remaining);
                    float desired_mag = fminf(move_peak, limit);
                    desired_mag = fmaxf(desired_mag, 1.0f);
                    desired = direction * desired_mag;

                    if (cur != 0.0f && ((cur > 0.0f) != (desired > 0.0f))) {
                        cur = approach_speed(cur, 0.0f, acc_step);
                    } else if (cur == 0.0f) {
                        set_direction = true;
                        cur = direction * fminf(desired_mag, POSITION_START_SPEED);
                    } else {
                        cur = approach_speed_smooth(cur, desired, acc_step);
                    }
                }

                taskENTER_CRITICAL(&s_move_mux);
                if (s_move_active[i] && s_move_generation[i] == generation &&
                    s_move_stop_requested[i] == stop_requested) {
                    if (stop_requested && cur == 0.0f) {
                        s_move_generation[i]++;
                        s_move_remaining[i] = 0;
                        s_move_stop_requested[i] = false;
                        s_move_status[i] = STEPPER_MOVE_CANCELLED;
                        s_target[i] = 0.0f;
                        s_speed[i] = 0.0f;
                        s_phase_inc[i] = 0;
                        s_phase[i] = 0;
                        s_move_active[i] = false;
                    } else {
                        if (set_direction) {
                            apply_dir((stepper_axis_t)i, direction > 0);
                        }
                        s_target[i] = desired;
                        s_speed[i] = cur;
                        s_phase_inc[i] = speed_to_phase_inc(cur);
                    }
                }
                taskEXIT_CRITICAL(&s_move_mux);
                continue;
            }

            float tgt = clamp_speed(target);
            bool set_direction = false;
            if (tgt == 0.0f) {
                cur = approach_speed(cur, 0.0f, acc_step);
            } else if (cur == 0.0f) {
                float start = fminf(fabsf(tgt), STEPPER_START_SPEED);
                set_direction = true;
                cur = copysignf(start, tgt);
            } else if ((cur > 0.0f) != (tgt > 0.0f)) {
                cur = approach_speed(cur, 0.0f, acc_step);
            } else {
                cur = approach_speed_smooth(cur, tgt, acc_step);
            }

            taskENTER_CRITICAL(&s_move_mux);
            if (!s_move_active[i] && s_move_generation[i] == generation) {
                if (set_direction) {
                    apply_dir((stepper_axis_t)i, tgt > 0.0f);
                }
                s_speed[i] = cur;
                s_phase_inc[i] = speed_to_phase_inc(cur);
            }
            taskEXIT_CRITICAL(&s_move_mux);
        }
    }
}

static esp_err_t init_gpio(void)
{
    gpio_config_t en_cfg = {
        .pin_bit_mask = (1ULL << PIN_EN_CHASSIS) | (1ULL << PIN_EN_LIFT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&en_cfg), TAG, "EN gpio config failed");
    gpio_set_level(PIN_EN_CHASSIS, 1);
    gpio_set_level(PIN_EN_LIFT, 1);
    s_en_chassis = false;
    s_en_lift = false;

    uint64_t mask = 0;
    for (int i = 0; i < AXIS_COUNT; ++i) {
        mask |= (1ULL << s_hw[i].step_pin) | (1ULL << s_hw[i].dir_pin);
    }
    gpio_config_t io_cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_cfg), TAG, "STEP/DIR gpio config failed");
    for (int i = 0; i < AXIS_COUNT; ++i) {
        gpio_set_level(s_hw[i].step_pin, 0);
        s_dir_invert[i] = s_hw[i].dir_invert;
        s_move_status[i] = STEPPER_MOVE_IDLE;
        apply_dir((stepper_axis_t)i, true);
    }
    return ESP_OK;
}

esp_err_t stepper_init(void)
{
    ESP_RETURN_ON_ERROR(init_gpio(), TAG, "gpio init failed");
    gptimer_config_t tcfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
        .intr_priority = 3,
        .flags.intr_shared = false,
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&tcfg, &s_timer), TAG, "gptimer create failed");
    gptimer_event_callbacks_t cbs = { .on_alarm = stepper_timer_isr };
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(s_timer, &cbs, NULL), TAG, "gptimer callback failed");
    gptimer_alarm_config_t acfg = {
        .alarm_count = STEPPER_TICK_US,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_RETURN_ON_ERROR(gptimer_set_alarm_action(s_timer, &acfg), TAG, "gptimer alarm failed");
    ESP_RETURN_ON_ERROR(gptimer_enable(s_timer), TAG, "gptimer enable failed");
    ESP_RETURN_ON_ERROR(gptimer_start(s_timer), TAG, "gptimer start failed");

    BaseType_t ret = xTaskCreatePinnedToCore(stepper_ramp_task, "step_ramp", 4096, NULL, 12, NULL, 0);
    if (ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "5-axis DDS ready: %d Hz tick, accel=%.0f sps^2", STEPPER_TICK_HZ, s_accel);
    return ESP_OK;
}

static void set_speed_locked(stepper_axis_t axis, float steps_per_sec)
{
    if (s_move_active[axis]) {
        cancel_move_locked(axis);
    } else {
        s_move_generation[axis]++;
    }
    if (steps_per_sec != 0.0f) {
        s_move_status[axis] = STEPPER_MOVE_IDLE;
    }
    s_target[axis] = clamp_speed(steps_per_sec);
}

void stepper_set_speed(stepper_axis_t axis, float steps_per_sec)
{
    if (axis >= AXIS_COUNT || !isfinite(steps_per_sec)) {
        return;
    }
    taskENTER_CRITICAL(&s_move_mux);
    set_speed_locked(axis, steps_per_sec);
    taskEXIT_CRITICAL(&s_move_mux);
}

void stepper_set_chassis_speeds(const float steps_per_sec[4])
{
    for (int i = 0; i < 4; ++i) {
        if (!isfinite(steps_per_sec[i])) {
            return;
        }
    }
    taskENTER_CRITICAL(&s_move_mux);
    for (int i = 0; i < 4; ++i) {
        set_speed_locked((stepper_axis_t)i, steps_per_sec[i]);
    }
    taskEXIT_CRITICAL(&s_move_mux);
}

float stepper_get_speed(stepper_axis_t axis)
{
    return (axis < AXIS_COUNT) ? s_speed[axis] : 0.0f;
}

float stepper_get_target(stepper_axis_t axis)
{
    return (axis < AXIS_COUNT) ? s_target[axis] : 0.0f;
}

void stepper_set_accel(float steps_per_sec2)
{
    if (!isfinite(steps_per_sec2)) {
        return;
    }
    if (steps_per_sec2 < STEPPER_MIN_ACCEL) {
        steps_per_sec2 = STEPPER_MIN_ACCEL;
    } else if (steps_per_sec2 > STEPPER_MAX_ACCEL) {
        steps_per_sec2 = STEPPER_MAX_ACCEL;
    }
    s_accel = steps_per_sec2;
}

float stepper_get_accel(void)
{
    return s_accel;
}

void stepper_set_dir_invert(stepper_axis_t axis, bool invert)
{
    if (axis < AXIS_COUNT) {
        s_dir_invert[axis] = invert;
    }
}

bool stepper_get_dir_invert(stepper_axis_t axis)
{
    return (axis < AXIS_COUNT) ? s_dir_invert[axis] : false;
}

void stepper_set_chassis_enable(bool enable)
{
    taskENTER_CRITICAL(&s_move_mux);
    if (!enable) {
        for (int i = 0; i < 4; ++i) {
            if (s_move_active[i]) {
                cancel_move_locked((stepper_axis_t)i);
            } else {
                s_move_generation[i]++;
            }
            s_move_stop_requested[i] = false;
            s_target[i] = 0.0f;
            s_speed[i] = 0.0f;
            s_phase_inc[i] = 0;
            s_phase[i] = 0;
            s_pulse_hold[i] = 0;
            gpio_set_level(s_hw[i].step_pin, 0);
        }
    }
    gpio_set_level(PIN_EN_CHASSIS, enable ? 0 : 1);
    s_en_chassis = enable;
    taskEXIT_CRITICAL(&s_move_mux);
}

void stepper_set_lift_enable(bool enable)
{
    taskENTER_CRITICAL(&s_move_mux);
    if (!enable) {
        if (s_move_active[AXIS_LIFT]) {
            cancel_move_locked(AXIS_LIFT);
        } else {
            s_move_generation[AXIS_LIFT]++;
        }
        s_move_stop_requested[AXIS_LIFT] = false;
        s_target[AXIS_LIFT] = 0.0f;
        s_speed[AXIS_LIFT] = 0.0f;
        s_phase_inc[AXIS_LIFT] = 0;
        s_phase[AXIS_LIFT] = 0;
        s_pulse_hold[AXIS_LIFT] = 0;
        gpio_set_level(s_hw[AXIS_LIFT].step_pin, 0);
    }
    gpio_set_level(PIN_EN_LIFT, enable ? 0 : 1);
    s_en_lift = enable;
    taskEXIT_CRITICAL(&s_move_mux);
}

bool stepper_chassis_enabled(void)
{
    return s_en_chassis;
}

bool stepper_lift_enabled(void)
{
    return s_en_lift;
}

esp_err_t stepper_start_move(stepper_axis_t axis, int32_t delta_steps, float peak_sps)
{
    if (axis >= AXIS_COUNT || delta_steps == 0 || !isfinite(peak_sps) || peak_sps <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    int64_t magnitude = (delta_steps > 0) ? (int64_t)delta_steps : -(int64_t)delta_steps;
    if (magnitude > INT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_OK;
    taskENTER_CRITICAL(&s_move_mux);
    if (s_move_active[axis] || fabsf(s_speed[axis]) > 1.0f) {
        result = ESP_ERR_INVALID_STATE;
    } else if ((axis == AXIS_LIFT && !s_en_lift) || (axis != AXIS_LIFT && !s_en_chassis)) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        s_move_generation[axis]++;
        s_target[axis] = 0.0f;
        s_speed[axis] = 0.0f;
        s_phase_inc[axis] = 0;
        s_phase[axis] = 0;
        s_move_peak[axis] = fminf(peak_sps, STEPPER_MAX_SPEED);
        s_move_direction[axis] = (delta_steps > 0) ? 1 : -1;
        s_move_remaining[axis] = (int32_t)magnitude;
        s_move_stop_requested[axis] = false;
        s_move_status[axis] = STEPPER_MOVE_RUNNING;
        s_move_active[axis] = true;
    }
    taskEXIT_CRITICAL(&s_move_mux);
    return result;
}

stepper_move_status_t stepper_get_move_status(stepper_axis_t axis)
{
    return (axis < AXIS_COUNT) ? s_move_status[axis] : STEPPER_MOVE_CANCELLED;
}

bool stepper_move_active(stepper_axis_t axis)
{
    return (axis < AXIS_COUNT) && s_move_active[axis];
}

int32_t stepper_get_move_remaining(stepper_axis_t axis)
{
    return (axis < AXIS_COUNT) ? s_move_remaining[axis] : 0;
}

int32_t stepper_get_position(stepper_axis_t axis)
{
    return (axis < AXIS_COUNT) ? s_position[axis] : 0;
}

esp_err_t stepper_set_position(stepper_axis_t axis, int32_t position)
{
    if (axis >= AXIS_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ESP_OK;
    taskENTER_CRITICAL(&s_move_mux);
    if (s_move_active[axis] || fabsf(s_speed[axis]) > 1.0f) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        s_position[axis] = position;
        s_move_status[axis] = STEPPER_MOVE_IDLE;
    }
    taskEXIT_CRITICAL(&s_move_mux);
    return result;
}

void stepper_stop(stepper_axis_t axis)
{
    if (axis >= AXIS_COUNT) {
        return;
    }
    taskENTER_CRITICAL(&s_move_mux);
    s_move_generation[axis]++;
    if (s_move_active[axis]) {
        /* 活动定步由 ramp 减速到零后再发布 CANCELLED，保持平滑停车。 */
        s_move_stop_requested[axis] = true;
    }
    s_target[axis] = 0.0f;
    taskEXIT_CRITICAL(&s_move_mux);
}

void stepper_stop_all(void)
{
    for (int i = 0; i < AXIS_COUNT; ++i) {
        stepper_stop((stepper_axis_t)i);
    }
}

void stepper_abort_all(void)
{
    taskENTER_CRITICAL(&s_move_mux);
    for (int i = 0; i < AXIS_COUNT; ++i) {
        if (s_move_active[i]) {
            cancel_move_locked((stepper_axis_t)i);
        } else {
            s_move_generation[i]++;
        }
        s_move_stop_requested[i] = false;
        s_target[i] = 0.0f;
        s_speed[i] = 0.0f;
        s_phase_inc[i] = 0;
        s_phase[i] = 0;
        s_pulse_hold[i] = 0;
        gpio_set_level(s_hw[i].step_pin, 0);
    }
    taskEXIT_CRITICAL(&s_move_mux);
}
