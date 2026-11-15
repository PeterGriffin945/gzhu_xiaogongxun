#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t min_black;
    uint8_t confirm_frames;
    uint8_t clear_max_black;
    uint8_t rearm_frames;
    uint32_t min_interval_ms;
} station_detector_config_t;

typedef struct {
    station_detector_config_t config;
    uint32_t last_seq;
    uint8_t candidate_count;
    uint8_t clear_count;
    bool armed;
    int64_t last_trigger_ms;
} station_detector_t;

void station_detector_init(station_detector_t *detector);
void station_detector_reset(station_detector_t *detector, int64_t now_ms);

/** 每个新 ADS 帧调用一次；只有完整横线事件上升沿返回 true。 */
bool station_detector_update(station_detector_t *detector, uint8_t black_mask,
                             uint32_t seq, int64_t now_ms);

#ifdef __cplusplus
}
#endif
