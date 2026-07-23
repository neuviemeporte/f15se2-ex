/*
 * utf8.c - Strict single-codepoint UTF-8 decoding.
 */
#include "utf8.h"

/* Decode one UTF-8 codepoint, replacing malformed input without reading past the terminator. */
int utf8DecodeCodepoint(const char *text, uint32_t *codepoint,
                        size_t *byte_count) {
    const unsigned char *bytes = (const unsigned char *)text;
    uint32_t value;
    size_t length;
    size_t index;

    if (!bytes || !codepoint || !byte_count || bytes[0] == 0) return 0;
    if (bytes[0] < 0x80) {
        *codepoint = bytes[0];
        *byte_count = 1;
        return 1;
    }
    if ((bytes[0] & 0xe0) == 0xc0) {
        value = bytes[0] & 0x1f;
        length = 2;
    } else if ((bytes[0] & 0xf0) == 0xe0) {
        value = bytes[0] & 0x0f;
        length = 3;
    } else if ((bytes[0] & 0xf8) == 0xf0) {
        value = bytes[0] & 0x07;
        length = 4;
    } else {
        return 0;
    }

    for (index = 1; index < length; ++index) {
        if (bytes[index] == 0 || (bytes[index] & 0xc0) != 0x80) return 0;
        value = (value << 6) | (bytes[index] & 0x3f);
    }
    if ((length == 2 && value < 0x80) ||
        (length == 3 && value < 0x800) ||
        (length == 4 && value < 0x10000) ||
        value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff)) {
        return 0;
    }

    *codepoint = value;
    *byte_count = length;
    return 1;
}
