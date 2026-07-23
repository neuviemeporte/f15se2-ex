#include "asound/asound_replacements.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

extern bool setGamePath(const char *path);
extern int loadF15DgtlBin(void);

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void append16(std::vector<unsigned char> &out, unsigned int value) {
    out.push_back((unsigned char)value);
    out.push_back((unsigned char)(value >> 8));
}

void append32(std::vector<unsigned char> &out, unsigned int value) {
    append16(out, value);
    append16(out, value >> 16);
}

void writePcm8MonoWav(const std::filesystem::path &path, int rate,
                      const std::vector<unsigned char> &samples) {
    std::vector<unsigned char> wav;
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
    append32(wav, 36u + (unsigned int)samples.size());
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    append32(wav, 16);
    append16(wav, 1);
    append16(wav, 1);
    append32(wav, (unsigned int)rate);
    append32(wav, (unsigned int)rate);
    append16(wav, 1);
    append16(wav, 8);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    append32(wav, (unsigned int)samples.size());
    wav.insert(wav.end(), samples.begin(), samples.end());
    std::ofstream(path, std::ios::binary)
        .write((const char *)wav.data(), (std::streamsize)wav.size());
}

void setReplacementRoot(const std::string &value) {
#if defined(_WIN32)
    _putenv_s("F15_REPLACEMENT_ROOT", value.c_str());
#else
    if (value.empty()) unsetenv("F15_REPLACEMENT_ROOT");
    else setenv("F15_REPLACEMENT_ROOT", value.c_str(), 1);
#endif
}

} // namespace

int main() {
    const auto root =
        std::filesystem::temp_directory_path() / "f15se2-ex-asound-replacement-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "sounds");
    writePcm8MonoWav(root / "sounds" / "voice_cue_000_sample0.wav",
                     11025, {0, 64, 128, 255});
    std::ofstream(root / "sounds" / "voice_cue_001_sample4.wav") << "not a WAV";

    setReplacementRoot(root.string());
    require(asound_load_replacement_cues() == 1,
            "loader accepts valid cues and ignores malformed optional cues");

    AsoundReplacementCue cue = {};
    require(asound_find_replacement_cue(0x0000u, 0x31f3u, &cue),
            "cue lookup uses the original inclusive sample range");
    require(cue.sample_count == 4 && cue.sample_rate == 11025,
            "cue preserves editable WAV length and sample rate");
    require(cue.samples[0] == 0 && cue.samples[2] == 128 && cue.samples[3] == 255,
            "cue preserves unsigned PCM bytes");
    require(!asound_find_replacement_cue(0x31f4u, 0x4796u, &cue),
            "malformed cue remains a legacy fallback");
    require(!asound_find_replacement_cue(1, 2, &cue),
            "unknown sample range remains a legacy fallback");

    require(setGamePath(root.string().c_str()), "modern-only game path is valid");
    require(loadF15DgtlBin() == 0x7d9e,
            "replacement cues enable digitized sound without F15DGTL.BIN");

    setReplacementRoot("");
    require(asound_load_replacement_cues() == 0,
            "reload without a replacement root clears prior cue data");
    require(!asound_find_replacement_cue(0x0000u, 0x31f3u, &cue),
            "cleared replacement no longer overrides the legacy cue");

    std::filesystem::remove_all(root);
    std::cout << "asound_replacements_behavior_tests passed\n";
    return 0;
}
