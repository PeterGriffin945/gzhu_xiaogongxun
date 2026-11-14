/**
 * @file ads1115.h
 * @brief 双 ADS1115 × 4 通道高速非阻塞采样（TCA9548A 通道 0/1）
 *
 * 探头编号与通道映射：
 *   TCA CH0 → ADS1115 @0x48 → A0..A3 → 探头 5,6,7,8
 *   TCA CH1 → ADS1115 @0x49 → A0..A3 → 探头 1,2,3,4
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 在总线上挂载两块 ADS1115（需先调用 i2c_bus_init） */
esp_err_t ads1115_init(void);

/** 创建 Core 1 采样任务 */
esp_err_t ads1115_start(void);

/**
 * @brief 拷贝最近一次 8 路电压
 * @param[out] v    电压数组，v[0] = 探头1 ... v[7] = 探头8，单位 V
 * @param[out] seq  采样序号（可为 NULL）
 * @return 是否已有有效数据
 */
bool ads1115_get_latest(float v[8], uint32_t *seq);

/** 最近一次采样的序号 */
uint32_t ads1115_get_seq(void);

/** 采样任务是否已产出数据 */
bool ads1115_is_ready(void);

/** 单路读取（0~7，0 对应探头 1） */
float ads1115_get_voltage(int index);

#ifdef __cplusplus
}
#endif
