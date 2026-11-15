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
    uint8_t base_stow;
    uint8_t base_pick;
    uint8_t base_tray;
    uint8_t base_place;
    uint8_t gripper_open;
    uint8_t gripper_closed;
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
