/**
 * @file lu9685.c
 * @brief LU9685 舵机板驱动（原生单字节协议）
 *
 * 注意：LU9685 不是 PCA9685。每次只需向"通道寄存器"写 1 字节角度：
 *   START | ADDR+W | CH | ANGLE | STOP
 * 初始化时依次探测 0x40 / 0x00 两个常见地址。
 */
#include "lu9685.h"

#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

#include "app_config.h"
#include "i2c_bus.h"

static const char *TAG = "lu9685";

#define LU9685_MAX_CH 16

static i2c_master_dev_handle_t s_dev;
static uint8_t s_addr;
static uint8_t s_angles[LU9685_MAX_CH];

static esp_err_t lu9685_write(uint8_t channel, uint8_t angle)
{
    if (!i2c_bus_lock(I2C_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = tca_select(TCA_CH_SERVO);
    if (err == ESP_OK) {
        uint8_t buf[2] = { channel, angle };
        err = i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
    }
    i2c_bus_unlock();
    return err;
}

esp_err_t lu9685_init(void)
{
    if (i2c_bus_handle() == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < LU9685_MAX_CH; ++i) {
        s_angles[i] = 90;
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
    lu9685_set_angle(LU9685_CH_BASE, 90);
    lu9685_set_angle(LU9685_CH_TRAY, 90);
    lu9685_set_angle(LU9685_CH_GRIPPER, 90);
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
    return (channel < LU9685_MAX_CH) ? s_angles[channel] : 90;
}
