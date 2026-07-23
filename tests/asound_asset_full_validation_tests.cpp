#include "asound/asound_model.h"
#include "asound/asound_music_replacement.h"
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
    const int loadedCues = asound_load_replacement_cues();
    require(loadedCues == asound_replacement_cue_count(),
            "not all replacement cue WAVs loaded");
    for (int index = 0; index < loadedCues; ++index) {
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

    static const AsoundU8 *const intro[ASOUND_STREAM_COUNT] = {
        asound_intro_voice0, asound_intro_voice1, asound_intro_voice2,
        asound_intro_voice3, asound_intro_voice4, asound_intro_voice5};
    static const AsoundU8 *const release[ASOUND_STREAM_COUNT] = {
        asound_release_voice0, asound_release_voice1, asound_release_voice2,
        asound_release_voice3, asound_release_voice4, asound_release_voice5};
    require(asound_reload_replacement_music(),
            "replacement intro music did not load");
    for (int phase = 0; phase < 2; ++phase) {
        for (int voice = 0; voice < ASOUND_STREAM_COUNT; ++voice) {
            const AsoundU8 *replacement = nullptr;
            size_t replacementSize = 0;
            const AsoundU8 *legacy = phase ? release[voice] : intro[voice];
            const size_t legacySize = phase
                ? asound_release_voice_length(voice)
                : asound_intro_voice_length(voice);
            require(asound_replacement_music_stream(
                        phase, voice, &replacement, &replacementSize),
                    "cannot enumerate replacement music stream");
            require(replacementSize == legacySize,
                    "replacement music stream length differs from legacy");
            require(std::memcmp(replacement, legacy, legacySize) == 0,
                    "replacement music stream bytes differ from legacy");
        }
    }

    setReplacementRoot(nullptr);
    std::cout << "asound_asset_full_validation_tests compared "
              << loadedCues << " cues and "
              << ASOUND_STREAM_COUNT * 2 << " music streams\n";
    return 0;
}
