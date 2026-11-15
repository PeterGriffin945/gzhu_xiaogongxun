#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "vision_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    vision_mode_t mode;
    vision_color_t color;
    bool detected;
    bool stable;
    uint16_t cx;
    uint16_t cy;
    uint16_t area;
    uint8_t confidence;
    uint8_t seq;
    int64_t received_ms;
} vision_result_t;

typedef struct {
    vision_mode_t requested_mode;
    vision_mode_t ack_mode;
    uint8_t last_command_seq;
    uint8_t last_ack_command_seq;
    uint8_t last_rx_seq;
    int64_t last_rx_ms;
    uint32_t valid_frames;
    uint32_t crc_errors;
    bool driver_ready;
} k230_link_status_t;

esp_err_t k230_link_init(void);
esp_err_t k230_link_set_mode(vision_mode_t mode);
bool k230_link_online(void);
bool k230_link_result_fresh(vision_mode_t mode);
void k230_link_get_result(vision_result_t *out);
void k230_link_get_status(k230_link_status_t *out);

#ifdef __cplusplus
}
#endif
