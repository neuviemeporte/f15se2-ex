/*
 * utf8.h - Small strict UTF-8 decoder shared by input and text rendering.
 */
#ifndef F15_UTF8_H
#define F15_UTF8_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decode one NUL-terminated UTF-8 sequence.
 *
 * Returns non-zero for a valid Unicode scalar value and writes its code point
 * and byte count. Truncated, overlong, surrogate, and out-of-range sequences
 * are rejected without reading beyond the first NUL byte.
 */
int utf8DecodeCodepoint(const char *text, uint32_t *codepoint,
                        size_t *byte_count);

#ifdef __cplusplus
}
#endif

#endif
