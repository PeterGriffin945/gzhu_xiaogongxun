#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "vision_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRAY_SLOT_COUNT 3   /* 实物托盘 3 槽，每格 120° */

typedef enum {
    TRAY_MOTION_IDLE = 0,
    TRAY_MOTION_RUNNING,
    TRAY_MOTION_DONE,
    TRAY_MOTION_ERROR,
} tray_motion_status_t;

typedef struct {
    bool zero_valid;
    uint8_t current_slot;
    uint8_t target_slot;
    uint8_t speed_angle;
    uint32_t index_duration_ms;
    tray_motion_status_t motion;
    vision_color_t colors[TRAY_SLOT_COUNT];
    esp_err_t last_error;
} tray_status_t;

void tray_controller_init(void);
esp_err_t tray_controller_set_zero(void);
void tray_controller_invalidate(void);
esp_err_t tray_controller_configure(uint8_t speed_angle, uint32_t index_duration_ms);
esp_err_t tray_controller_start_next(void);
esp_err_t tray_controller_start_to(uint8_t target_slot);
void tray_controller_update(void);
void tray_controller_abort(void);
bool tray_controller_busy(void);
void tray_controller_get_status(tray_status_t *out);
esp_err_t tray_controller_mark_loaded(uint8_t slot, vision_color_t color);
esp_err_t tray_controller_mark_empty(uint8_t slot);
int tray_controller_find_color(vision_color_t color);
uint8_t tray_forward_steps(uint8_t current_slot, uint8_t target_slot);

#ifdef __cplusplus
}
#endif
