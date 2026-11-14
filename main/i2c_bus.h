/**
 * @file i2c_bus.h
 * @brief I2C 主总线 + TCA9548A 8 路复用器管理
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 复位 TCA9548A 并初始化 I2C 主总线（400kHz） */
esp_err_t i2c_bus_init(void);

/** 获取总线句柄，用于 add_device */
i2c_master_bus_handle_t i2c_bus_handle(void);

/** 选择 TCA9548A 通道 (0~7)，同一时刻只开通一路 */
esp_err_t tca_select(uint8_t channel);

/** 关闭所有通道 */
esp_err_t tca_deselect(void);

/** TCA9548A 是否在线 */
bool tca_available(void);

/**
 * @brief 获取总线互斥锁
 * @note 一次"选通道 + 多字节读写"序列必须整体加锁，防止多任务交叉切换通道
 */
bool i2c_bus_lock(uint32_t timeout_ms);
void i2c_bus_unlock(void);

#ifdef __cplusplus
}
#endif
