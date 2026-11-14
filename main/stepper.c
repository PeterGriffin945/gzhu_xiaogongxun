/**
 * @file stepper.c
 * @brief 5 轴步进电机 DDS 脉冲发生器
 *
 * 设计要点：
 *  - GPTimer 硬件定时器 20kHz（50us），在 ISR 中用 32 位相位累加器
 *    （DDS）同时产生 5 路 STEP，STEP 高电平保持 1 个 tick（50us >= 3us）。
 *  - 每个 tick 先拉低上一拍的 STEP，再根据相位溢出拉高本拍 STEP，
 *    因此相邻两个脉冲之间至少有 50us 低电平，最高步频 10kHz。
 *  - 1kHz 任务负责梯形加减速，把速度换算成相位增量写入 ISR；
 *    起步时直接跨过 200 步/秒的低速共振区。
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

static const char *TAG = "stepper";

typedef struct {
    gpio_num_t step_pin;
    gpio_num_t dir_pin;
    bool dir_invert;      /* 左侧对置安装 → 逻辑方向取反 */
} axis_hw_t;

static const axis_hw_t s_hw[AXIS_COUNT] = {
    [AXIS_RR]   = { PIN_M1_RR_STEP,   PIN_M1_RR_DIR,   false },
    [AXIS_RF]   = { PIN_M2_RF_STEP,   PIN_M2_RF_DIR,   false },
    [AXIS_LF]   = { PIN_M3_LF_STEP,   PIN_M3_LF_DIR,   true  },
    [AXIS_LR]   = { PIN_M4_LR_STEP,   PIN_M4_LR_DIR,   true  },
    [AXIS_LIFT] = { PIN_M5_LIFT_STEP, PIN_M5_LIFT_DIR, false },
};

/* ---------- ISR 共享数据 ---------- */
static volatile int32_t s_phase_inc[AXIS_COUNT];   /* 每 tick 相位增量 */
static volatile float   s_target[AXIS_COUNT];      /* 目标速度 步/秒 */
static volatile float   s_speed[AXIS_COUNT];       /* 当前速度 步/秒 */
static uint32_t         s_phase[AXIS_COUNT];       /* 32 位相位累加器 */
static uint8_t          s_pulse_hold[AXIS_COUNT];  /* 1 = 本拍 STEP 为高 */

static bool s_dir_invert[AXIS_COUNT];

static volatile bool s_en_chassis;
static volatile bool s_en_lift;
static volatile float s_accel = STEPPER_DEFAULT_ACCEL;

static gptimer_handle_t s_timer;

/* ======================= 硬件定时器 ISR ======================= */
static bool IRAM_ATTR stepper_timer_isr(gptimer_handle_t timer,
                                        const gptimer_alarm_event_data_t *edata,
                                        void *user_ctx)
{
    (void)timer;
    (void)edata;
    (void)user_ctx;

    for (int i = 0; i < AXIS_COUNT; ++i) {
        /* 相位累加必须每拍都执行，否则高步频时会丢失步数 */
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
            /* 上一拍的高电平到此结束（脉宽 = 50us） */
            s_pulse_hold[i] = 0;
            gpio_set_level(s_hw[i].step_pin, 0);
        }

        /* 最高 8kHz 时相邻脉冲间隔 >= 2 tick，不会出现连续高电平 */
        if (fire && !was_high) {
            gpio_set_level(s_hw[i].step_pin, 1);
            s_pulse_hold[i] = 1;
        }
    }
    return false;
}

/* ======================= 方向 / 速度工具 ======================= */
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

/* ======================= 1kHz 速度斜坡任务 ======================= */
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
            dt = 0.05f;      /* 任务被长时间阻塞时限制步进，避免速度突变 */
        }

        float acc_step = s_accel * dt;

        for (int i = 0; i < AXIS_COUNT; ++i) {
            float cur = s_speed[i];
            float tgt = clamp_speed(s_target[i]);

            if (tgt == 0.0f) {
                /* 减速到 0 */
                if (fabsf(cur) <= acc_step) {
                    cur = 0.0f;
                } else {
                    cur -= copysignf(acc_step, cur);
                }
            } else if (cur == 0.0f) {
                /* 从零起步：直接跨过 200 步/秒死区 */
                float start = fminf(fabsf(tgt), STEPPER_START_SPEED);
                apply_dir((stepper_axis_t)i, tgt > 0.0f);
                cur = copysignf(start, tgt);
            } else if ((cur > 0.0f) != (tgt > 0.0f)) {
                /* 方向相反：先制动到 0，下一拍再换向，严禁带速换向 */
                if (fabsf(cur) <= acc_step) {
                    cur = 0.0f;
                } else {
                    cur -= copysignf(acc_step, cur);
                }
            } else {
                float mag = fabsf(cur);
                float tmag = fabsf(tgt);
                if (mag < tmag) {
                    mag = fminf(mag + acc_step, tmag);
                } else {
                    mag = fmaxf(mag - acc_step, tmag);
                }
                cur = copysignf(mag, cur);
            }

            s_speed[i] = cur;
            s_phase_inc[i] = speed_to_phase_inc(cur);
        }
    }
}

/* ======================= 初始化 ======================= */
static esp_err_t init_gpio(void)
{
    /* 先配置 EN，并立即拉高（脱机释放，防上电发热） */
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
        mask |= (1ULL << s_hw[i].step_pin);
        mask |= (1ULL << s_hw[i].dir_pin);
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
        .resolution_hz = 1000000,       /* 1MHz，计数 50 = 50us */
        .intr_priority = 3,
        .flags.intr_shared = false,
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&tcfg, &s_timer), TAG, "gptimer create failed");

    gptimer_event_callbacks_t cbs = {
        .on_alarm = stepper_timer_isr,
    };
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(s_timer, &cbs, NULL),
                        TAG, "gptimer callback failed");

    gptimer_alarm_config_t acfg = {
        .alarm_count = STEPPER_TICK_US,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_RETURN_ON_ERROR(gptimer_set_alarm_action(s_timer, &acfg),
                        TAG, "gptimer alarm failed");
    ESP_RETURN_ON_ERROR(gptimer_enable(s_timer), TAG, "gptimer enable failed");
    ESP_RETURN_ON_ERROR(gptimer_start(s_timer), TAG, "gptimer start failed");

    BaseType_t ret = xTaskCreatePinnedToCore(stepper_ramp_task, "step_ramp",
                                             4096, NULL, 12, NULL, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "create ramp task failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "5-axis DDS ready: %d Hz tick, start=%.0f sps, accel=%.0f sps^2",
             STEPPER_TICK_HZ, STEPPER_START_SPEED, s_accel);
    return ESP_OK;
}

/* ======================= 对外接口 ======================= */
void stepper_set_speed(stepper_axis_t axis, float steps_per_sec)
{
    if (axis >= AXIS_COUNT) {
        return;
    }
    s_target[axis] = clamp_speed(steps_per_sec);
}

void stepper_set_chassis_speeds(const float steps_per_sec[4])
{
    for (int i = 0; i < 4; ++i) {
        s_target[i] = clamp_speed(steps_per_sec[i]);
    }
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
    if (!enable) {
        for (int i = 0; i < 4; ++i) {
            s_target[i] = 0.0f;
        }
    }
    gpio_set_level(PIN_EN_CHASSIS, enable ? 0 : 1);
    s_en_chassis = enable;
}

void stepper_set_lift_enable(bool enable)
{
    if (!enable) {
        s_target[AXIS_LIFT] = 0.0f;
    }
    gpio_set_level(PIN_EN_LIFT, enable ? 0 : 1);
    s_en_lift = enable;
}

bool stepper_chassis_enabled(void)
{
    return s_en_chassis;
}

bool stepper_lift_enabled(void)
{
    return s_en_lift;
}

void stepper_stop(stepper_axis_t axis)
{
    if (axis < AXIS_COUNT) {
        s_target[axis] = 0.0f;
    }
}

void stepper_stop_all(void)
{
    for (int i = 0; i < AXIS_COUNT; ++i) {
        s_target[i] = 0.0f;
    }
}

void stepper_abort_all(void)
{
    for (int i = 0; i < AXIS_COUNT; ++i) {
        s_target[i] = 0.0f;
        s_speed[i] = 0.0f;
        s_phase_inc[i] = 0;
        s_pulse_hold[i] = 0;
        gpio_set_level(s_hw[i].step_pin, 0);
    }
}
