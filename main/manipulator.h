#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MANIPULATOR_OP_STOW = 0,
    MANIPULATOR_OP_PICK_AND_LOAD,
    MANIPULATOR_OP_UNLOAD_AND_PLACE,
} manipulator_operation_t;

typedef enum {
    MANIPULATOR_IDLE = 0,
    MANIPULATOR_RUNNING,
    MANIPULATOR_DONE,
    MANIPULATOR_ERROR,
} manipulator_status_t;

typedef struct {
    int32_t z_transport;
    int32_t z_ground_pick;
    int32_t z_tray;
    int32_t z_ground_place;
    int32_t z_max;
    uint16_t base_stow;     /* 逻辑角度 0~360（底座按 360° 行程标定） */
    uint16_t base_pick;
    uint16_t base_tray;
    uint16_t base_place;
    uint8_t gripper_open;
    uint8_t gripper_closed;
    /* CH0 为 360° 连续旋转舵机：用速度角 + 时间控制转角 */
    uint8_t base_speed_angle;   /* <90 正转，>90 反转 */
    uint16_t base_sweep_ms;     /* 逻辑 0~180° 行程时间 */
    uint16_t base_settle_ms;
    uint16_t gripper_settle_ms;
    float lift_speed_sps;
} manipulator_config_t;

void manipulator_init(void);
void manipulator_configure(const manipulator_config_t *config);
esp_err_t manipulator_start(manipulator_operation_t operation);
manipulator_status_t manipulator_update(void);
bool manipulator_busy(void);
esp_err_t manipulator_last_error(void);
void manipulator_abort(void);
uint8_t manipulator_current_step(void);

#ifdef __cplusplus
}
#endif
