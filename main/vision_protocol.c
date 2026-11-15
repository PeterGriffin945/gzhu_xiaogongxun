#include "vision_protocol.h"

#include <string.h>

uint8_t vision_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

void vision_parser_init(vision_parser_t *parser)
{
    if (parser != NULL) {
        parser->length = 0;
    }
}

static void drop_first_byte(vision_parser_t *parser)
{
    if (parser->length == 0) {
        return;
    }
    parser->length--;
    if (parser->length > 0) {
        memmove(parser->data, parser->data + 1, parser->length);
    }
}

bool vision_parser_feed(vision_parser_t *parser, uint8_t byte, vision_frame_t *out)
{
    if (parser == NULL || out == NULL) {
        return false;
    }
    if (parser->length >= VISION_MAX_FRAME) {
        parser->length = 0;
    }
    parser->data[parser->length++] = byte;

    while (parser->length > 0) {
        if (parser->data[0] != 0xA5) {
            drop_first_byte(parser);
            continue;
        }
        if (parser->length == 1) {
            return false;
        }
        if (parser->data[1] != 0x5A) {
            drop_first_byte(parser);
            continue;
        }
        if (parser->length < 6) {
            return false;
        }
        uint8_t payload_length = parser->data[5];
        if (parser->data[2] != VISION_PROTOCOL_VERSION || payload_length > VISION_MAX_PAYLOAD) {
            drop_first_byte(parser);
            continue;
        }
        uint8_t frame_length = payload_length + 7;
        if (parser->length < frame_length) {
            return false;
        }
        if (vision_crc8(parser->data + 2, frame_length - 3) != parser->data[frame_length - 1]) {
            drop_first_byte(parser);
            continue;
        }

        out->type = parser->data[3];
        out->seq = parser->data[4];
        out->length = payload_length;
        if (payload_length > 0) {
            memcpy(out->payload, parser->data + 6, payload_length);
        }
        uint8_t remaining = parser->length - frame_length;
        if (remaining > 0) {
            memmove(parser->data, parser->data + frame_length, remaining);
        }
        parser->length = remaining;
        return true;
    }
    return false;
}

bool vision_encode_frame(uint8_t type, uint8_t seq, const uint8_t *payload,
                         uint8_t length, uint8_t *out, size_t *out_length)
{
    if (out == NULL || out_length == NULL || *out_length < length + 7 ||
        length > VISION_MAX_PAYLOAD || (length > 0 && payload == NULL)) {
        return false;
    }
    out[0] = 0xA5;
    out[1] = 0x5A;
    out[2] = VISION_PROTOCOL_VERSION;
    out[3] = type;
    out[4] = seq;
    out[5] = length;
    if (length > 0) {
        memcpy(out + 6, payload, length);
    }
    out[length + 6] = vision_crc8(out + 2, length + 4);
    *out_length = length + 7;
    return true;
}
