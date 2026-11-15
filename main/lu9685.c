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
} lu9685_pulse_t;

static i2c_master_dev_handle_t s_dev;
static uint8_t s_addr;
static uint8_t s_angles[LU9685_MAX_CH];
static uint8_t s_center[LU9685_MAX_CH];
/* 连续旋转通道的逻辑角度：由定时转动累积，供“走多少度”换算使用。 */
static uint8_t s_target[LU9685_MAX_CH];
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
    taskEXIT_CRITICAL(&s_pulse_mux);
}

static void lu9685_pulse_task(void *arg)
{
    (void)arg;
    while (1) {
        int64_t now = esp_timer_get_time();
        for (int i = 0; i < LU9685_MAX_CH; ++i) {
            uint8_t stop = 0;
            bool fire = false;
            taskENTER_CRITICAL(&s_pulse_mux);
            if (s_pulse[i].active && now >= s_pulse[i].deadline_us) {
                stop = s_pulse[i].stop_angle;
                fire = true;
                s_pulse[i].active = false;
            }
            taskEXIT_CRITICAL(&s_pulse_mux);

            if (fire) {
                esp_err_t err = lu9685_write((uint8_t)i, stop);
                if (err == ESP_OK) {
                    s_angles[i] = stop;
                } else if (s_pulse[i].retries < LU9685_STOP_RETRIES) {
                    /* 停止写失败时快速有界重试；重试期间仍保留点动记录。 */
                    s_pulse[i].retries++;
                    taskENTER_CRITICAL(&s_pulse_mux);
                    s_pulse[i].active = true;
                    s_pulse[i].deadline_us = now + 5000;
                    taskEXIT_CRITICAL(&s_pulse_mux);
                } else {
                    s_pulse[i].faulted = true;
                    ESP_LOGE(TAG, "pulse CH%d stop failed after retries", i);
                }
            }
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
        s_pulse[i].active = false;
        s_pulse[i].faulted = false;
    }

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

    lu9685_set_angle(LU9685_CH_BASE, LU9685_DEFAULT_CENTER);
    lu9685_set_angle(LU9685_CH_TRAY, LU9685_DEFAULT_CENTER);
    lu9685_set_angle(LU9685_CH_GRIPPER, LU9685_DEFAULT_CENTER);
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
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = lu9685_write(channel, angle);
    if (err == ESP_OK) {
        s_angles[channel] = angle;
        /* 只有新角度真正写成功才取消点动定时，否则保留停止定时器，
           避免一次 I2C 失败让连续舵机失去自动停止保护。 */
        clear_pulse(channel);
    }
    return err;
}

uint8_t lu9685_get_angle(uint8_t channel)
{
    return (channel < LU9685_MAX_CH) ? s_angles[channel] : LU9685_DEFAULT_CENTER;
}

esp_err_t lu9685_set_center(uint8_t channel, uint8_t angle)
{
    if (channel >= LU9685_MAX_CH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (angle > 180) {
        angle = 180;
    }
    s_center[channel] = angle;
    return ESP_OK;
}

uint8_t lu9685_get_center(uint8_t channel)
{
    return (channel < LU9685_MAX_CH) ? s_center[channel] : LU9685_DEFAULT_CENTER;
}

esp_err_t lu9685_pulse_start(uint8_t channel, uint8_t speed_angle,
                             uint32_t duration_ms, int stop_angle)
{
    if (channel >= LU9685_MAX_CH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_pulse_task_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (speed_angle > 180) {
        speed_angle = 180;
    }
    if (duration_ms > 60000) {
        duration_ms = 60000;
    }
    uint8_t stop = (stop_angle < 0) ? s_center[channel]
                                   : (uint8_t)((stop_angle > 180) ? 180 : stop_angle);

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

esp_err_t lu9685_move_timed(uint8_t channel, uint8_t target_angle,
                            uint8_t speed_angle, uint32_t ms_per_180,
                            int stop_angle, uint32_t *duration_ms_out)
{
    if (channel >= LU9685_MAX_CH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (target_angle > 180) {
        target_angle = 180;
    }
    if (speed_angle == 0 || speed_angle >= 180) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ms_per_180 == 0) {
        ms_per_180 = ARM_BASE_SWEEP_MS_DEFAULT;
    } else if (ms_per_180 > 60000U) {
        ms_per_180 = 60000U;
    }

    int16_t delta = (int16_t)target_angle - (int16_t)s_target[channel];
    if (delta == 0) {
        if (duration_ms_out != NULL) {
            *duration_ms_out = 0;
        }
        return ESP_OK;
    }
    uint32_t steps = (uint32_t)((delta > 0) ? delta : -delta);
    uint32_t duration = (steps * ms_per_180 + 90U) / 180U;   /* 四舍五入 */
    uint8_t run_speed = (delta > 0) ? speed_angle : (uint8_t)(180 - speed_angle);

    esp_err_t err = lu9685_pulse_start(channel, run_speed, duration, stop_angle);
    if (err == ESP_OK) {
        s_target[channel] = target_angle;
    }
    if (duration_ms_out != NULL) {
        *duration_ms_out = duration;
    }
    return err;
}

uint8_t lu9685_get_target_angle(uint8_t channel)
{
    return (channel < LU9685_MAX_CH) ? s_target[channel] : LU9685_DEFAULT_CENTER;
}

esp_err_t lu9685_set_target_angle(uint8_t channel, uint8_t angle)
{
    if (channel >= LU9685_MAX_CH) {
        return ESP_ERR_INVALID_ARG;
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
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = lu9685_write(channel, s_center[channel]);
    if (err == ESP_OK) {
        s_angles[channel] = s_center[channel];
    } else {
        s_pulse[channel].faulted = true;
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
        if (s_pulse[i].active || i == LU9685_CH_TRAY) {
            lu9685_pulse_cancel((uint8_t)i);
        }
    }
}
