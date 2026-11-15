#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VISION_PROTOCOL_VERSION 0x01
#define VISION_MAX_PAYLOAD 32
#define VISION_MAX_FRAME (VISION_MAX_PAYLOAD + 7)

typedef enum {
    VISION_MSG_COMMAND = 0x01,
    VISION_MSG_HEARTBEAT = 0x02,
    VISION_MSG_ACK = 0x03,
    VISION_MSG_DETECTION = 0x10,
} vision_message_type_t;

typedef enum {
    VISION_MODE_IDLE = 0,
    VISION_MODE_MATERIAL = 1,
    VISION_MODE_RING = 2,
    VISION_MODE_TRAY = 3,
} vision_mode_t;

typedef enum {
    VISION_COLOR_UNKNOWN = 0,
    VISION_COLOR_RED = 1,
    VISION_COLOR_GREEN = 2,
    VISION_COLOR_BLUE = 3,
} vision_color_t;

typedef struct {
    uint8_t type;
    uint8_t seq;
    uint8_t payload[VISION_MAX_PAYLOAD];
    uint8_t length;
} vision_frame_t;

typedef struct {
    uint8_t data[VISION_MAX_FRAME];
    uint8_t length;
} vision_parser_t;

uint8_t vision_crc8(const uint8_t *data, size_t length);
void vision_parser_init(vision_parser_t *parser);
bool vision_parser_feed(vision_parser_t *parser, uint8_t byte, vision_frame_t *out);
bool vision_encode_frame(uint8_t type, uint8_t seq, const uint8_t *payload,
                         uint8_t length, uint8_t *out, size_t *out_length);

#ifdef __cplusplus
}
#endif
