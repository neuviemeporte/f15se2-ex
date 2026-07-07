#ifndef INTTYPE_H
#define INTTYPE_H

#include <stdint.h>
typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;

typedef int64_t int64;
typedef int32_t int32;
typedef int16_t int16;
typedef int8_t int8;

static inline int abs16Compat(int value) {
    enum { INTTYPE_SIGNED_WORD_MIN = -0x8000 };
    /* MSC 16-bit abs(-32768) overflows and leaves the value negative. */
    value = (int16)value;
    return value == INTTYPE_SIGNED_WORD_MIN ? INTTYPE_SIGNED_WORD_MIN : (value < 0 ? -value : value);
}
/*
 * Unaligned multi-byte access for packed asset / save-file buffers (model
 * streams in *.3D*, the HallFame roster record). The
 * original game read these with raw `*(int16*)p` casts, which is fine on x86
 * but is undefined behaviour on alignment-strict targets (ARM, 3DS, VR) — a
 * real trap there, not just a UBSan note. memcpy is the portable form and the
 * compiler folds it back to a single load/store where the ISA allows it.
 * Only genuinely packed byte streams need these; in-memory structs should be
 * accessed via their named fields instead.
 */
#include <string.h>
static inline uint16 rdU16(const void *p) {
    uint16 v;
    memcpy(&v, p, sizeof v);
    return v;
}
static inline uint32 rdU32(const void *p) {
    uint32 v;
    memcpy(&v, p, sizeof v);
    return v;
}
static inline int16 rdI16(const void *p) {
    int16 v;
    memcpy(&v, p, sizeof v);
    return v;
}
static inline int32 rdI32(const void *p) {
    int32 v;
    memcpy(&v, p, sizeof v);
    return v;
}
static inline void wrU16(void *p, uint16 v) { memcpy(p, &v, sizeof v); }
static inline void wrU32(void *p, uint32 v) { memcpy(p, &v, sizeof v); }
static inline void wrI16(void *p, int16 v) { memcpy(p, &v, sizeof v); }
static inline void wrI32(void *p, int32 v) { memcpy(p, &v, sizeof v); }

#endif /* INTTYPE_H */
