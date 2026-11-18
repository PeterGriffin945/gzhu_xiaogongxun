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
    uint16_t base_stow;     /* MG996R 180° 位置舵机，绝对角度 0~180 */
    uint16_t base_pick;
    uint16_t base_tray;
    uint16_t base_place;
    /* CH8 夹爪为连续旋转舵机：开/合各用一个速度角 + 通电时间 */
    uint8_t gripper_open;       /* 张开方向速度角（<90 或 >90，按机械方向标定） */
    uint8_t gripper_closed;     /* 夹紧方向速度角 */
    uint16_t gripper_open_ms;   /* 张开通电时间 */
    uint16_t gripper_close_ms;  /* 夹紧通电时间（建议顶到限位后再多 150~250ms） */
    /* CH0 底座已改为 180° 位置舵机；以下字段仅为旧连续舵机保留，不再生效 */
    uint8_t base_speed_angle;
    uint16_t base_sweep_ms;
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
