/**
 * @file lu9685.c
 * @brief LU9685 舵机板驱动，支持角度/脉宽协议与连续舵机安全点动
 */
#include "lu9685.h"

#include <math.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "i2c_bus.h"

#define LU9685_MAX_CH 16
#define LU9685_PULSE_TICK_MS 5
#define LU9685_DEFAULT_CENTER 90
#define LU9685_STOP_RETRIES 3

static const char *TAG = "lu9685";

typedef struct {
    bool active;
    int64_t deadline_us;
    uint8_t stop_angle;
    uint8_t retries;
    bool faulted;
    uint8_t confirm_left;    /* 停止角成功后再补写的次数 */
    int64_t confirm_us;
    uint32_t gen;            /* 代次：新命令/取消后丢弃旧的停止写入 */
} lu9685_pulse_t;

static i2c_master_dev_handle_t s_dev;
static uint8_t s_addr;
static uint8_t s_angles[LU9685_MAX_CH];      /* 最近一次写入的原始角度 0~180 */
static uint8_t s_center[LU9685_MAX_CH];
/* 逻辑角度（度）：位置舵机为绝对角，连续舵机为定时点动累积角。 */
static uint16_t s_target[LU9685_MAX_CH];
static lu9685_mode_t s_mode[LU9685_MAX_CH];
static float s_pos_deg[LU9685_MAX_CH];
static float s_min_deg[LU9685_MAX_CH];
static float s_max_deg[LU9685_MAX_CH];
static float s_travel_deg[LU9685_MAX_CH];
static volatile uint8_t s_proto = LU9685_PROTO_ANGLE;
static portMUX_TYPE s_pulse_mux = portMUX_INITIALIZER_UNLOCKED;
static lu9685_pulse_t s_pulse[LU9685_MAX_CH];
static bool s_pulse_task_ready;

static esp_err_t lu9685_write(uint8_t channel, uint8_t angle)
{
    uint8_t mode = s_proto;
    uint8_t buf[3];
    size_t len;
    if (mode == LU9685_PROTO_ANGLE) {
        buf[0] = channel;
        buf[1] = angle;
        len = 2;
    } else {
        uint16_t us = (uint16_t)lroundf(500.0f + (float)angle * (2000.0f / 180.0f));
        buf[0] = channel;
        if (mode == LU9685_PROTO_PULSE_LO) {
            buf[1] = (uint8_t)(us & 0xFF);
            buf[2] = (uint8_t)(us >> 8);
        } else {
            buf[1] = (uint8_t)(us >> 8);
            buf[2] = (uint8_t)(us & 0xFF);
        }
        len = 3;
    }

    if (!i2c_bus_lock(I2C_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = tca_select(TCA_CH_SERVO);
    if (err == ESP_OK) {
        err = i2c_master_transmit(s_dev, buf, len, I2C_TIMEOUT_MS);
    }
    i2c_bus_unlock();
    return err;
}

static void clear_pulse(uint8_t channel)
{
    taskENTER_CRITICAL(&s_pulse_mux);
    s_pulse[channel].active = false;
    s_pulse[channel].confirm_left = 0;
    s_pulse[channel].gen++;
    taskEXIT_CRITICAL(&s_pulse_mux);
}

static float clamp_deg(uint8_t channel, float deg)
{
    if (deg < s_min_deg[channel]) {
        return s_min_deg[channel];
    }
    if (deg > s_max_deg[channel]) {
        return s_max_deg[channel];
    }
    return deg;
}

/* 逻辑角度 0~travel ↔ LU9685 原始角度 0~180 */
static uint8_t raw_from_deg(uint8_t channel, float deg)
{
    float raw = deg * 180.0f / s_travel_deg[channel];
    if (raw < 0.0f) {
        raw = 0.0f;
    } else if (raw > 180.0f) {
        raw = 180.0f;
    }
    return (uint8_t)lroundf(raw);
}

static float deg_from_raw(uint8_t channel, uint8_t raw)
{
    return (float)raw * s_travel_deg[channel] / 180.0f;
}

/* 连续舵机的“停止角”：CH0 底座必须写 90° 才停；其余用校准中位 */
static uint8_t stop_angle_for(uint8_t channel)
{
    if (channel == LU9685_CH_BASE) {
        return (uint8_t)SERVO_BASE_STOP_DEG;
    }
    return s_center[channel];
}

static void lu9685_pulse_task(void *arg)
{
    (void)arg;
    while (1) {
        int64_t now = esp_timer_get_time();
        for (int i = 0; i < LU9685_MAX_CH; ++i) {
            uint8_t stop = 0;
            bool fire = false;
            uint8_t confirm = 0;
            uint32_t gen = 0;

            taskENTER_CRITICAL(&s_pulse_mux);
            if (s_pulse[i].active && now >= s_pulse[i].deadline_us) {
                stop = s_pulse[i].stop_angle;
                fire = true;
                gen = s_pulse[i].gen;
                s_pulse[i].active = false;
            } else if (s_pulse[i].confirm_left > 0 && now >= s_pulse[i].confirm_us) {
                stop = s_pulse[i].stop_angle;
                confirm = s_pulse[i].confirm_left;
                gen = s_pulse[i].gen;
                s_pulse[i].confirm_left = 0;
            }
            taskEXIT_CRITICAL(&s_pulse_mux);

            if (!fire && confirm == 0) {
                continue;
            }
            /* 若期间有新命令/取消，旧的停止角作废，不能把新动作刹停 */
            taskENTER_CRITICAL(&s_pulse_mux);
            bool valid = (s_pulse[i].gen == gen);
            taskEXIT_CRITICAL(&s_pulse_mux);
            if (!valid) {
                continue;
            }

            esp_err_t err = lu9685_write((uint8_t)i, stop);

            taskENTER_CRITICAL(&s_pulse_mux);
            if (s_pulse[i].gen != gen) {
                /* 写的过程中被新命令接管：不覆盖新状态 */
                taskEXIT_CRITICAL(&s_pulse_mux);
                continue;
            }
            if (err == ESP_OK) {
                s_angles[i] = stop;
                if (fire) {
                    /* 停止角成功后追加 2~3 次确认写入，防止单次丢包 */
                    s_pulse[i].confirm_left = SERVO_STOP_CONFIRM_COUNT;
                    s_pulse[i].confirm_us = now + (int64_t)SERVO_STOP_CONFIRM_MS * 1000;
                } else if (confirm > 1) {
                    s_pulse[i].confirm_left = (uint8_t)(confirm - 1);
                    s_pulse[i].confirm_us = now + (int64_t)SERVO_STOP_CONFIRM_MS * 1000;
                }
            } else {
                /* 停不下来就绝不放弃：标记故障并每 500ms 继续写停止角 */
                s_pulse[i].faulted = true;
                if (fire) {
                    s_pulse[i].active = true;
                    s_pulse[i].deadline_us = now + (int64_t)SERVO_STOP_RETRY_MS * 1000;
                } else {
                    s_pulse[i].confirm_left = confirm;
                    s_pulse[i].confirm_us = now + (int64_t)SERVO_STOP_RETRY_MS * 1000;
                }
                ESP_LOGE(TAG, "pulse CH%d stop write failed, retrying", i);
            }
            taskEXIT_CRITICAL(&s_pulse_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(LU9685_PULSE_TICK_MS));
    }
}

esp_err_t lu9685_init(void)
{
    if (i2c_bus_handle() == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    for (int i = 0; i < LU9685_MAX_CH; ++i) {
        s_angles[i] = LU9685_DEFAULT_CENTER;
        s_center[i] = LU9685_DEFAULT_CENTER;
        s_target[i] = LU9685_DEFAULT_CENTER;
        s_mode[i] = LU9685_MODE_POSITION;
        s_pos_deg[i] = (float)LU9685_DEFAULT_CENTER;
        s_travel_deg[i] = 180.0f;
        s_min_deg[i] = 0.0f;
        s_max_deg[i] = 180.0f;
        s_pulse[i].active = false;
        s_pulse[i].faulted = false;
    }
    /* CH0 底座：MG996R 180° 位置舵机，写绝对角度即到位并保持 */
    s_mode[LU9685_CH_BASE] = LU9685_MODE_POSITION;
    s_travel_deg[LU9685_CH_BASE] = 180.0f;
    s_min_deg[LU9685_CH_BASE] = 0.0f;
    s_max_deg[LU9685_CH_BASE] = 180.0f;
    s_pos_deg[LU9685_CH_BASE] = 90.0f;
    s_target[LU9685_CH_BASE] = 90;
    /* CH8 夹爪：旧 MG996 连续旋转舵机，用速度角 + 通电时间开合 */
    s_mode[LU9685_CH_GRIPPER] = LU9685_MODE_CONTINUOUS;
    s_travel_deg[LU9685_CH_GRIPPER] = SERVO_GRIPPER_TRAVEL_DEG;
    /* CH4 物料盘：MG90S 连续旋转 + 定时分度（3 槽，每格 120°） */
    s_mode[LU9685_CH_TRAY] = LU9685_MODE_CONTINUOUS;

    BaseType_t ret = xTaskCreatePinnedToCore(lu9685_pulse_task, "srv_pulse", 4096, NULL, 3, NULL, 1);
    s_pulse_task_ready = (ret == pdPASS);
    if (!s_pulse_task_ready) {
        ESP_LOGE(TAG, "pulse task unavailable; continuous servo jog disabled");
    }

    uint8_t candidates[2] = { LU9685_ADDR_A, LU9685_ADDR_B };
    bool found = false;
    if (!i2c_bus_lock(I2C_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = tca_select(TCA_CH_SERVO);
    if (err == ESP_OK) {
        for (int i = 0; i < 2; ++i) {
            if (i2c_master_probe(i2c_bus_handle(), candidates[i], I2C_TIMEOUT_MS) == ESP_OK) {
                s_addr = candidates[i];
                found = true;
                break;
            }
        }
        if (found) {
            i2c_device_config_t cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = s_addr,
                .scl_speed_hz = I2C_FREQ_HZ,
            };
            err = i2c_master_bus_add_device(i2c_bus_handle(), &cfg, &s_dev);
        }
    }
    i2c_bus_unlock();

    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        s_dev = NULL;
        return err;
    }

    lu9685_set_angle(LU9685_CH_BASE, LU9685_DEFAULT_CENTER);   /* 位置舵机回中位 */
    lu9685_set_angle(LU9685_CH_TRAY, LU9685_DEFAULT_CENTER);   /* 连续舵机中位停转 */
    lu9685_set_angle(LU9685_CH_GRIPPER, LU9685_DEFAULT_CENTER);/* 连续舵机中位停转 */
    ESP_LOGI(TAG, "LU9685 @0x%02X ready", s_addr);
    return ESP_OK;
}

bool lu9685_available(void)
{
    return s_dev != NULL;
}

bool lu9685_pulse_ready(void)
{
    return s_pulse_task_ready;
}

uint8_t lu9685_address(void)
{
    return s_addr;
}

esp_err_t lu9685_set_protocol(uint8_t mode)
{
    if (mode > LU9685_PROTO_PULSE_HI) {
        return ESP_ERR_INVALID_ARG;
    }
    s_proto = mode;
    return ESP_OK;
}

uint8_t lu9685_get_protocol(void)
{
    return s_proto;
}

esp_err_t lu9685_set_angle(uint8_t channel, uint8_t angle)
{
    if (channel >= LU9685_MAX_CH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (angle > 180) {
        angle = 180;
    }
    if (s_mode[channel] == LU9685_MODE_POSITION) {
        /* 位置舵机：原始角度按行程换算成逻辑角度后统一限位 */
        return lu9685_move_deg(channel, deg_from_raw(channel, angle));
    }
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = lu9685_write(channel, angle);
    if (err == ESP_OK) {
        s_angles[channel] = angle;
        /* 只有新角度真正写成功才取消点动定时，否则保留停止定时器，
           避免一次 I2C 失败让连续舵机失去自动停止保护。 */
        clear_pulse(channel);
        /* 连续舵机被直接写入非停止角时，兜底挂一个最长停止定时：
           即使调用方忘了计时，也一定会在 SERVO_MAX_JOG_MS 后写回停止角。 */
        if (angle != stop_angle_for(channel)) {
            taskENTER_CRITICAL(&s_pulse_mux);
            if (!s_pulse[channel].active) {
                s_pulse[channel].deadline_us =
                    esp_timer_get_time() + (int64_t)SERVO_MAX_JOG_MS * 1000;
                s_pulse[channel].stop_angle = stop_angle_for(channel);
                s_pulse[channel].retries = 0;
                s_pulse[channel].faulted = false;
                s_pulse[channel].active = true;
            }
            taskEXIT_CRITICAL(&s_pulse_mux);
        }
    }
    return err;
}

uint8_t lu9685_get_angle(uint8_t channel)
{
    if (channel >= LU9685_MAX_CH) {
        return LU9685_DEFAULT_CENTER;
    }
    if (s_mode[channel] == LU9685_MODE_POSITION) {
        return raw_from_deg(channel, s_pos_deg[channel]);
    }
    return s_angles[channel];
}

esp_err_t lu9685_set_mode(uint8_t channel, lu9685_mode_t mode)
{
    if (channel >= LU9685_MAX_CH || mode > LU9685_MODE_CONTINUOUS) {
        return ESP_ERR_INVALID_ARG;
    }
    s_mode[channel] = mode;
    if (mode == LU9685_MODE_POSITION) {
        clear_pulse(channel);
        s_pos_deg[channel] = deg_from_raw(channel, s_angles[channel]);
    }
    return ESP_OK;
}

lu9685_mode_t lu9685_get_mode(uint8_t channel)
{
    return (channel < LU9685_MAX_CH) ? s_mode[channel] : LU9685_MODE_POSITION;
}

esp_err_t lu9685_set_limits_deg(uint8_t channel, float min_deg, float max_deg)
{
    if (channel >= LU9685_MAX_CH || !isfinite(min_deg) || !isfinite(max_deg) ||
        min_deg < 0.0f || max_deg > s_travel_deg[channel] || min_deg >= max_deg) {
        return ESP_ERR_INVALID_ARG;
    }
    s_min_deg[channel] = min_deg;
    s_max_deg[channel] = max_deg;
    /* 不主动移动：只约束后续命令，避免标定时舵机突然动作 */
    return ESP_OK;
}

void lu9685_get_limits_deg(uint8_t channel, float *min_deg, float *max_deg)
{
    if (channel >= LU9685_MAX_CH) {
        if (min_deg != NULL) {
            *min_deg = 0.0f;
        }
        if (max_deg != NULL) {
            *max_deg = 180.0f;
        }
        return;
    }
    if (min_deg != NULL) {
        *min_deg = s_min_deg[channel];
    }
    if (max_deg != NULL) {
        *max_deg = s_max_deg[channel];
    }
}

esp_err_t lu9685_move_deg(uint8_t channel, float deg)
{
    if (channel >= LU9685_MAX_CH || !isfinite(deg)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mode[channel] != LU9685_MODE_POSITION) {
        return ESP_ERR_INVALID_STATE;
    }
    float target = clamp_deg(channel, deg);
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t raw = raw_from_deg(channel, target);
    esp_err_t err = lu9685_write(channel, raw);
    if (err == ESP_OK) {
        s_angles[channel] = raw;
        s_pos_deg[channel] = target;
        clear_pulse(channel);
    }
    return err;
}

esp_err_t lu9685_step_deg(uint8_t channel, float delta_deg)
{
    if (channel >= LU9685_MAX_CH || !isfinite(delta_deg)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mode[channel] != LU9685_MODE_POSITION) {
        return ESP_ERR_INVALID_STATE;
    }
    return lu9685_move_deg(channel, s_pos_deg[channel] + delta_deg);
}

float lu9685_get_pos_deg(uint8_t channel)
{
    if (channel >= LU9685_MAX_CH) {
        return 0.0f;
    }
    if (s_mode[channel] == LU9685_MODE_POSITION) {
        return s_pos_deg[channel];
    }
    return (float)s_target[channel];   /* 连续通道：定时点动累积的逻辑角度 */
}

esp_err_t lu9685_set_center(uint8_t channel, uint8_t angle)
{
    if (channel >= LU9685_MAX_CH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (channel == LU9685_CH_BASE) {
        /* 底座连续舵机只有 90° 会停，禁止校准成其他值 */
        s_center[channel] = (uint8_t)SERVO_BASE_STOP_DEG;
        return ESP_OK;
    }
    if (angle > 180) {
        angle = 180;
    }
    s_center[channel] = angle;
    return ESP_OK;
}

uint8_t lu9685_get_center(uint8_t channel)
{
    if (channel >= LU9685_MAX_CH) {
        return LU9685_DEFAULT_CENTER;
    }
    if (channel == LU9685_CH_BASE) {
        return (uint8_t)SERVO_BASE_STOP_DEG;
    }
    return s_center[channel];
}

esp_err_t lu9685_pulse_start(uint8_t channel, uint8_t speed_angle,
                             uint32_t duration_ms, int stop_angle)
{
    if (channel >= LU9685_MAX_CH) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 位置舵机禁止“速度角 + 持续时间”，否则会一直转；请用 move_deg/step_deg */
    if (s_mode[channel] == LU9685_MODE_POSITION) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_pulse_task_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (speed_angle > 180) {
        speed_angle = 180;
    }
    if (duration_ms > SERVO_MAX_JOG_MS) {
        duration_ms = SERVO_MAX_JOG_MS;   /* 单次点动硬上限，必须自动停止 */
    }
    /* 底座无论调用方传什么，停止角一律 90°；其他连续通道用校准中位 */
    uint8_t stop;
    if (channel == LU9685_CH_BASE) {
        stop = (uint8_t)SERVO_BASE_STOP_DEG;
    } else {
        stop = (stop_angle < 0) ? s_center[channel]
                                : (uint8_t)((stop_angle > 180) ? 180 : stop_angle);
    }

    esp_err_t err = lu9685_set_angle(channel, speed_angle);
    if (err != ESP_OK) {
        return err;
    }
    taskENTER_CRITICAL(&s_pulse_mux);
    s_pulse[channel].deadline_us = esp_timer_get_time() + (int64_t)duration_ms * 1000;
    s_pulse[channel].stop_angle = stop;
    s_pulse[channel].retries = 0;
    s_pulse[channel].faulted = false;
    s_pulse[channel].active = true;
    taskEXIT_CRITICAL(&s_pulse_mux);
    return ESP_OK;
}

esp_err_t lu9685_move_timed(uint8_t channel, uint16_t target_angle,
                            uint8_t speed_angle, uint32_t ms_per_180,
                            int stop_angle, uint32_t *duration_ms_out)
{
    if (channel >= LU9685_MAX_CH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mode[channel] == LU9685_MODE_POSITION) {
        /* MG996 类位置舵机：写一次目标角度即到位并保持，绝不持续转 */
        if (duration_ms_out != NULL) {
            *duration_ms_out = 0;
        }
        return lu9685_move_deg(channel, (float)target_angle);
    }
    /* 连续通道：目标先按软限位夹紧（CH0 0~360°），再按标定的
       ms_per_180 / speed_angle 换算转动时间；单次时长硬上限 SERVO_MAX_JOG_MS。 */
    float target = clamp_deg(channel, (float)target_angle);
    if (speed_angle == 0 || speed_angle >= 180) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ms_per_180 == 0) {
        ms_per_180 = ARM_BASE_SWEEP_MS_DEFAULT;
    } else if (ms_per_180 > 60000U) {
        ms_per_180 = 60000U;
    }

    int32_t current = (int32_t)s_target[channel];
    int32_t delta = (int32_t)lroundf(target) - current;
    if (delta == 0) {
        if (duration_ms_out != NULL) {
            *duration_ms_out = 0;
        }
        return ESP_OK;
    }
    uint32_t steps = (uint32_t)((delta > 0) ? delta : -delta);
    uint32_t max_steps = (uint32_t)((uint64_t)SERVO_MAX_JOG_MS * 180ULL / ms_per_180);
    if (max_steps == 0) {
        max_steps = 1;
    }
    if (steps > max_steps) {
        /* 标定值过小也不允许一次转太久，防止“一直转” */
        steps = max_steps;
        delta = (delta > 0) ? (int32_t)steps : -(int32_t)steps;
    }
    uint32_t duration = (steps * ms_per_180 + 90U) / 180U;   /* 四舍五入 */
    if (duration > SERVO_MAX_JOG_MS) {
        duration = SERVO_MAX_JOG_MS;
    }
    uint8_t run_speed = (delta > 0) ? speed_angle : (uint8_t)(180 - speed_angle);

    esp_err_t err = lu9685_pulse_start(channel, run_speed, duration, stop_angle);
    if (err == ESP_OK) {
        s_target[channel] = (uint16_t)(current + delta);
    }
    if (duration_ms_out != NULL) {
        *duration_ms_out = duration;
    }
    return err;
}

uint16_t lu9685_get_target_angle(uint8_t channel)
{
    if (channel >= LU9685_MAX_CH) {
        return LU9685_DEFAULT_CENTER;
    }
    if (s_mode[channel] == LU9685_MODE_POSITION) {
        return (uint16_t)lroundf(s_pos_deg[channel]);
    }
    return s_target[channel];
}

esp_err_t lu9685_set_target_angle(uint8_t channel, uint16_t angle)
{
    if (channel >= LU9685_MAX_CH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mode[channel] == LU9685_MODE_POSITION) {
        float target = clamp_deg(channel, (float)angle);
        s_pos_deg[channel] = target;
        s_angles[channel] = raw_from_deg(channel, target);
        return ESP_OK;
    }
    s_target[channel] = (angle > 180) ? 180 : angle;
    return ESP_OK;
}

esp_err_t lu9685_pulse_cancel(uint8_t channel)
{
    if (channel >= LU9685_MAX_CH) {
        return ESP_ERR_INVALID_ARG;
    }
    clear_pulse(channel);
    /* 位置舵机本来就保持角度，不需要写中位（写中位反而会产生一次动作） */
    if (s_mode[channel] == LU9685_MODE_POSITION) {
        return ESP_OK;
    }
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t stop = stop_angle_for(channel);
    esp_err_t err = lu9685_write(channel, stop);
    taskENTER_CRITICAL(&s_pulse_mux);
    s_pulse[channel].stop_angle = stop;
    s_pulse[channel].faulted = (err != ESP_OK);
    /* 一次成功也再补写几次；失败则持续重试，确保“停不下来”不会发生 */
    s_pulse[channel].confirm_left = SERVO_STOP_CONFIRM_COUNT;
    s_pulse[channel].confirm_us = esp_timer_get_time() +
        (int64_t)((err == ESP_OK) ? SERVO_STOP_CONFIRM_MS : SERVO_STOP_RETRY_MS) * 1000;
    taskEXIT_CRITICAL(&s_pulse_mux);
    if (err == ESP_OK) {
        s_angles[channel] = stop;
    }
    return err;
}

bool lu9685_pulse_active(uint8_t channel)
{
    return (channel < LU9685_MAX_CH) && s_pulse[channel].active;
}

bool lu9685_pulse_faulted(uint8_t channel)
{
    return (channel < LU9685_MAX_CH) && s_pulse[channel].faulted;
}

void lu9685_pulse_stop_all(void)
{
    for (int i = 0; i < LU9685_MAX_CH; ++i) {
        if (s_mode[i] == LU9685_MODE_POSITION) {
            clear_pulse((uint8_t)i);
            continue;
        }
        if (s_pulse[i].active || i == LU9685_CH_TRAY) {
            lu9685_pulse_cancel((uint8_t)i);
        }
    }
}

/* 位置舵机保持当前位置（急停用）：仅取消定时器，不产生新动作。
   连续通道的停止角补写/重试必须保留，否则急停时可能停不下来。 */
void lu9685_hold_all(void)
{
    for (int i = 0; i < LU9685_MAX_CH; ++i) {
        if (s_mode[i] == LU9685_MODE_POSITION) {
            clear_pulse((uint8_t)i);
        }
    }
}
