#include "station_detector.h"

#include <stddef.h>

static uint8_t count_black(uint8_t mask)
{
    uint8_t count = 0;
    for (int i = 0; i < 8; ++i) {
        count += (mask & (1U << i)) ? 1U : 0U;
    }
    return count;
}

void station_detector_init(station_detector_t *detector)
{
    if (detector == NULL) {
        return;
    }
    detector->config.min_black = 6;
    detector->config.confirm_frames = 2;
    detector->config.clear_max_black = 3;
    detector->config.rearm_frames = 3;
    detector->config.min_interval_ms = 800;
    station_detector_reset(detector, 0);
}

void station_detector_reset(station_detector_t *detector, int64_t now_ms)
{
    if (detector == NULL) {
        return;
    }
    detector->last_seq = 0;
    detector->candidate_count = 0;
    detector->clear_count = 0;
    detector->armed = false;
    /* 复位后不应再享受 min_interval 冷却：否则每段行程开头都有 0.37~1.2m 盲区。
       重新武装仍由 rearm_frames 保证，不会在起点这条线上立刻误触发。 */
    detector->last_trigger_ms = now_ms - (int64_t)detector->config.min_interval_ms;
}

bool station_detector_update(station_detector_t *detector, uint8_t black_mask,
                             uint32_t seq, int64_t now_ms)
{
    if (detector == NULL || seq == detector->last_seq) {
        return false;
    }
    detector->last_seq = seq;
    uint8_t count = count_black(black_mask);

    if (!detector->armed) {
        /* 只有重新看到“存在但不宽”的正常线才重新武装，丢线不算离站。 */
        if (count > 0 && count <= detector->config.clear_max_black) {
            if (detector->clear_count < 255) {
                detector->clear_count++;
            }
        } else {
            detector->clear_count = 0;
        }
        if (detector->clear_count >= detector->config.rearm_frames) {
            detector->armed = true;
            detector->candidate_count = 0;
        }
        return false;
    }

    bool wide = count >= detector->config.min_black &&
                (black_mask & 0x0F) != 0 && (black_mask & 0xF0) != 0;
    if (!wide) {
        detector->candidate_count = 0;
        return false;
    }
    if (detector->candidate_count < 255) {
        detector->candidate_count++;
    }
    if (detector->candidate_count < detector->config.confirm_frames ||
        now_ms - detector->last_trigger_ms < (int64_t)detector->config.min_interval_ms) {
        return false;
    }

    detector->armed = false;
    detector->candidate_count = 0;
    detector->clear_count = 0;
    detector->last_trigger_ms = now_ms;
    return true;
}
