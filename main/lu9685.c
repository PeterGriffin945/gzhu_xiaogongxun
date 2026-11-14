/**
 * @file lu9685.c
 * @brief LU9685 舵机板驱动（单字节角度 / 双字节脉宽 双协议 + 点动定时）
 *
 * 注意：LU9685 不是 PCA9685。协议格式为 [通道寄存器, 数据...]，一次写操作
 * 切换一次通道。默认单字节角度：START | ADDR+W | CH | ANGLE | STOP。
 * 若模块固件使用脉宽寄存器，可切换到双字节 500~2500us 模式。
 *
 * 360° 连续旋转舵机：90 附近停转，偏离中位越远转速越快，方向随偏离方向
 * 改变。用 lu9685_pulse_start() 定时点动即可用时间控制转角。
 */
#include "lu9685.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "i2c_bus.h"

static const char *TAG = "lu9685";

#define LU9685_MAX_CH       16
#define LU9685_PULSE_TICK_MS 5
#define LU9685_DEFAULT_CENTER 90

typedef struct {
    bool     active;
    int64_t  deadline_us;
    uint8_t  stop_angle;
} lu9685_pulse_t;

static i2c_master_dev_handle_t s_dev;
static uint8_t s_addr;
static uint8_t s_angles[LU9685_MAX_CH];
static uint8_t s_center[LU9685_MAX_CH];
static volatile uint8_t s_proto = LU9685_PROTO_ANGLE;

static portMUX_TYPE s_pulse_mux = portMUX_INITIALIZER_UNLOCKED;
static lu9685_pulse_t s_pulse[LU9685_MAX_CH];

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
        /* 0~180° → 500~2500us */
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

/* ---------------- 点动定时任务 ---------------- */
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
                s_pulse[i].active = false;
                stop = s_pulse[i].stop_angle;
                fire = true;
            }
            taskEXIT_CRITICAL(&s_pulse_mux);

            if (fire) {
                ESP_LOGI(TAG, "pulse CH%d done -> %u", i, stop);
                lu9685_set_angle((uint8_t)i, stop);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(LU9685_PULSE_TICK_MS));
    }
}

static esp_err_t lu9685_start_pulse_task(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(lu9685_pulse_task, "srv_pulse",
                                             4096, NULL, 3, NULL, 1);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t lu9685_init(void)
{
    if (i2c_bus_handle() == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < LU9685_MAX_CH; ++i) {
        s_angles[i] = LU9685_DEFAULT_CENTER;
        s_center[i] = LU9685_DEFAULT_CENTER;
        s_pulse[i].active = false;
    }

    esp_err_t task_err = lu9685_start_pulse_task();
    if (task_err != ESP_OK) {
        ESP_LOGE(TAG, "create pulse task failed");
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
        ESP_LOGW(TAG, "LU9685 not found on TCA CH%d (tried 0x%02X/0x%02X)",
                 TCA_CH_SERVO, LU9685_ADDR_A, LU9685_ADDR_B);
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LU9685 add device failed: %s", esp_err_to_name(err));
        s_dev = NULL;
        return err;
    }

    ESP_LOGI(TAG, "LU9685 @0x%02X ready (CH%d base / CH%d tray / CH%d gripper)",
             s_addr, LU9685_CH_BASE, LU9685_CH_TRAY, LU9685_CH_GRIPPER);

    /* 上电默认中位，避免舵机猛地打到极限 */
    lu9685_set_angle(LU9685_CH_BASE, LU9685_DEFAULT_CENTER);
    lu9685_set_angle(LU9685_CH_TRAY, LU9685_DEFAULT_CENTER);
    lu9685_set_angle(LU9685_CH_GRIPPER, LU9685_DEFAULT_CENTER);
    return ESP_OK;
}

bool lu9685_available(void)
{
    return s_dev != NULL;
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
    ESP_LOGI(TAG, "protocol -> %s",
             mode == LU9685_PROTO_ANGLE ? "angle" :
             (mode == LU9685_PROTO_PULSE_LO ? "pulse(lo-hi)" : "pulse(hi-lo)"));
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
    s_angles[channel] = angle;

    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = lu9685_write(channel, angle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set ch%u=%u failed: %s", channel, angle, esp_err_to_name(err));
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
    ESP_LOGI(TAG, "center CH%u -> %u", channel, angle);
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
    if (speed_angle > 180) {
        speed_angle = 180;
    }
    if (duration_ms > 60000) {
        duration_ms = 60000;
    }

    uint8_t stop;
    if (stop_angle < 0) {
        stop = s_center[channel];
    } else {
        stop = (uint8_t)((stop_angle > 180) ? 180 : stop_angle);
    }

    esp_err_t err = lu9685_set_angle(channel, speed_angle);
    if (err != ESP_OK) {
        return err;
    }

    int64_t deadline = esp_timer_get_time() + (int64_t)duration_ms * 1000;
    taskENTER_CRITICAL(&s_pulse_mux);
    s_pulse[channel].active = true;
    s_pulse[channel].deadline_us = deadline;
    s_pulse[channel].stop_angle = stop;
    taskEXIT_CRITICAL(&s_pulse_mux);

    ESP_LOGI(TAG, "pulse CH%u speed=%u dur=%ums stop=%u", channel, speed_angle,
             (unsigned)duration_ms, stop);
    return ESP_OK;
}

void lu9685_pulse_stop_all(void)
{
    for (int i = 0; i < LU9685_MAX_CH; ++i) {
        bool was;
        taskENTER_CRITICAL(&s_pulse_mux);
        was = s_pulse[i].active;
        s_pulse[i].active = false;
        taskEXIT_CRITICAL(&s_pulse_mux);
        if (was) {
            lu9685_set_angle((uint8_t)i, s_center[i]);
        }
    }
}
