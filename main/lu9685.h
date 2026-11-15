/**
 * @file lu9685.h
 * @brief LU9685 16 路舵机驱动与连续舵机安全点动
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LU9685_PROTO_ANGLE = 0,
    LU9685_PROTO_PULSE_LO = 1,
    LU9685_PROTO_PULSE_HI = 2,
} lu9685_proto_t;

esp_err_t lu9685_init(void);
bool lu9685_available(void);
bool lu9685_pulse_ready(void);
uint8_t lu9685_address(void);
esp_err_t lu9685_set_protocol(uint8_t mode);
uint8_t lu9685_get_protocol(void);
esp_err_t lu9685_set_angle(uint8_t channel, uint8_t angle);
uint8_t lu9685_get_angle(uint8_t channel);
esp_err_t lu9685_set_center(uint8_t channel, uint8_t angle);
uint8_t lu9685_get_center(uint8_t channel);
esp_err_t lu9685_pulse_start(uint8_t channel, uint8_t speed_angle,
                             uint32_t duration_ms, int stop_angle);
esp_err_t lu9685_pulse_cancel(uint8_t channel);
bool lu9685_pulse_active(uint8_t channel);
bool lu9685_pulse_faulted(uint8_t channel);
void lu9685_pulse_stop_all(void);

#ifdef __cplusplus
}
#endif
