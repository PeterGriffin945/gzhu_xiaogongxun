/**
 * @file lu9685.h
 * @brief LU9685 国产 16 路舵机驱动（TCA9548A 通道 2）
 *
 * 支持三种下发协议，可在运行时切换（部分批次模块固件不同）：
 *   - LU9685_PROTO_ANGLE     ：[ch, angle]                 单字节角度 0~180
 *   - LU9685_PROTO_PULSE_LO  ：[ch, pulse_lo, pulse_hi]    双字节脉宽 500~2500us
 *   - LU9685_PROTO_PULSE_HI  ：[ch, pulse_hi, pulse_lo]    双字节脉宽 大端
 *
 * 地址：上电自动探测 0x40 或 0x00
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LU9685_PROTO_ANGLE    = 0,   /* 单字节角度（默认） */
    LU9685_PROTO_PULSE_LO = 1,   /* 双字节脉宽，低字节在前 */
    LU9685_PROTO_PULSE_HI = 2,   /* 双字节脉宽，高字节在前 */
} lu9685_proto_t;

esp_err_t lu9685_init(void);
bool lu9685_available(void);
uint8_t lu9685_address(void);

/** 切换下发协议 0/1/2，立即生效 */
esp_err_t lu9685_set_protocol(uint8_t mode);
uint8_t lu9685_get_protocol(void);

/** 设置通道角度 0~180（超出自动限幅），按当前协议下发 */
esp_err_t lu9685_set_angle(uint8_t channel, uint8_t angle);

/** 读取缓存角度；未设置的通道返回 90 */
uint8_t lu9685_get_angle(uint8_t channel);

/** 中位（360° 舵机的停止值）校准，默认 90，建议 85~95 */
esp_err_t lu9685_set_center(uint8_t channel, uint8_t angle);
uint8_t lu9685_get_center(uint8_t channel);

/**
 * @brief 点动：先下发 speed_angle，duration_ms 后自动回到中位停止
 *
 * 用于 360° 连续旋转舵机，用时间控制转角。同一通道再次调用会重新计时。
 * 定时由后台任务执行，浏览器断开也不会失控。
 *
 * @param stop_angle 停止角，<0 表示使用该通道校准的中位值
 */
esp_err_t lu9685_pulse_start(uint8_t channel, uint8_t speed_angle,
                             uint32_t duration_ms, int stop_angle);

/** 取消所有通道的点动计时，并立即回到各自中位 */
void lu9685_pulse_stop_all(void);

#ifdef __cplusplus
}
#endif
