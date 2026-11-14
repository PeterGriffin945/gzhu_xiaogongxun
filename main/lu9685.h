/**
 * @file lu9685.h
 * @brief LU9685 国产 16 路舵机驱动（TCA9548A 通道 2）
 *
 * 协议：单字节寄存器写 —— [通道号, 角度 0~180]
 * 地址：上电自动探测 0x40 或 0x00
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lu9685_init(void);
bool lu9685_available(void);
uint8_t lu9685_address(void);

/** 设置通道角度 0~180（超出自动限幅） */
esp_err_t lu9685_set_angle(uint8_t channel, uint8_t angle);

/** 读取缓存角度；未设置的通道返回 90 */
uint8_t lu9685_get_angle(uint8_t channel);

#ifdef __cplusplus
}
#endif
