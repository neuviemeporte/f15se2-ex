#include <cstddef>
#include <cstdint>
#include <cstdio>

extern "C" {
#include "shared/utf8.h"
}

static int failures;

static void expectDecode(const char *text, uint32_t expectedCodepoint,
                         size_t expectedBytes) {
    uint32_t codepoint = 0;
    size_t byteCount = 0;

    if (!utf8DecodeCodepoint(text, &codepoint, &byteCount) ||
        codepoint != expectedCodepoint || byteCount != expectedBytes) {
        std::fprintf(stderr,
                     "decode failed: expected U+%04x/%zu, got U+%04x/%zu\n",
                     expectedCodepoint, expectedBytes, codepoint, byteCount);
        ++failures;
    }
}

static void expectReject(const char *text) {
    uint32_t codepoint = 0;
    size_t byteCount = 0;

    if (utf8DecodeCodepoint(text, &codepoint, &byteCount)) {
        std::fprintf(stderr, "invalid UTF-8 decoded as U+%04x/%zu\n",
                     codepoint, byteCount);
        ++failures;
    }
}

int main() {
    expectDecode("A", 0x41, 1);
    expectDecode("\xd0\xba", 0x043a, 2);
    expectDecode("\xe2\x82\xac", 0x20ac, 3);
    expectDecode("\xf0\x9f\x9b\xa9", 0x1f6e9, 4);

    expectReject("");
    expectReject("\xc0\xaf");          /* Overlong two-byte encoding. */
    expectReject("\xe0\x80\xaf");      /* Overlong three-byte encoding. */
    expectReject("\xed\xa0\x80");      /* UTF-16 surrogate U+D800. */
    expectReject("\xf4\x90\x80\x80");  /* Beyond Unicode U+10FFFF. */
    expectReject("\xe2\x82");          /* Truncated sequence. */
    expectReject("\x80");              /* Continuation without a lead byte. */

    {
        uint32_t codepoint = 0;
        size_t byteCount = 0;
        if (utf8DecodeCodepoint(nullptr, &codepoint, &byteCount) ||
            utf8DecodeCodepoint("A", nullptr, &byteCount) ||
            utf8DecodeCodepoint("A", &codepoint, nullptr)) {
            std::fprintf(stderr, "null argument was accepted\n");
            ++failures;
        }
    }

    return failures ? 1 : 0;
}
