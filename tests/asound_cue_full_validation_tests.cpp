#include "asound/asound_replacements.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#if !defined(F15_ORIGINAL_ASSETS)
#define F15_ORIGINAL_ASSETS ""
#endif
#if !defined(F15_CONVERTED_ASSETS)
#define F15_CONVERTED_ASSETS ""
#endif

namespace {

constexpr int kLegacySampleRate = 7850;

void require(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void setReplacementRoot(const std::filesystem::path *root) {
#if defined(_WIN32)
    _putenv_s("F15_REPLACEMENT_ROOT", root ? root->string().c_str() : "");
#else
    if (root) setenv("F15_REPLACEMENT_ROOT", root->string().c_str(), 1);
    else unsetenv("F15_REPLACEMENT_ROOT");
#endif
}

std::vector<unsigned char> readFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    const std::filesystem::path originalRoot = F15_ORIGINAL_ASSETS;
    const std::filesystem::path convertedRoot = F15_CONVERTED_ASSETS;
    const std::vector<unsigned char> blob =
        readFile(originalRoot / "F15DGTL.BIN");
    require(!blob.empty(), "cannot read original F15DGTL.BIN");

    setReplacementRoot(&convertedRoot);
    const int loaded = asound_load_replacement_cues();
    require(loaded == asound_replacement_cue_count(),
            "not all replacement cue WAVs loaded");
    for (int index = 0; index < loaded; ++index) {
        AsoundU16 start = 0;
        AsoundU16 end = 0;
        AsoundReplacementCue cue = {};
        require(asound_replacement_cue_at(index, &start, &end, &cue),
                "cannot enumerate loaded replacement cue");
        const size_t count = (size_t)end - start + 1u;
        require(cue.sample_rate == kLegacySampleRate,
                "replacement cue sample rate differs from legacy");
        require(cue.sample_count == (int)count && end < blob.size(),
                "replacement cue range or length differs from legacy");
        require(std::memcmp(cue.samples, blob.data() + start, count) == 0,
                "replacement cue PCM differs from legacy");
    }

    setReplacementRoot(nullptr);
    std::cout << "asound_cue_full_validation_tests compared "
              << loaded << " cues\n";
    return 0;
}
