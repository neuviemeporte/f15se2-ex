#include "asound/asound_music_replacement.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void setReplacementRoot(const std::string &value) {
#if defined(_WIN32)
    _putenv_s("F15_REPLACEMENT_ROOT", value.c_str());
#else
    if (value.empty()) unsetenv("F15_REPLACEMENT_ROOT");
    else setenv("F15_REPLACEMENT_ROOT", value.c_str(), 1);
#endif
}

void writeMusic(const std::filesystem::path &path, bool complete) {
    std::ofstream out(path);
    out << "{\"streams\":[";
    bool first = true;
    for (int phase = 0; phase < 2; ++phase) {
        for (int voice = 0; voice < ASOUND_STREAM_COUNT; ++voice) {
            if (!complete && phase == 1 && voice == ASOUND_STREAM_COUNT - 1) continue;
            if (!first) out << ',';
            first = false;
            out << "{\"source_symbol\":\"asound_"
                << (phase ? "release" : "intro") << "_voice" << voice
                << "\",\"stream_bytes\":[" << (phase ? 200 : 100) + voice
                << ",0]}";
        }
    }
    out << "]}";
}

} // namespace

int main() {
    const auto root =
        std::filesystem::temp_directory_path() / "f15se2-ex-asound-music-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "sounds");
    const auto musicPath = root / "sounds" / "intro_music.asound.json";
    writeMusic(musicPath, true);
    setReplacementRoot(root.string());

    require(asound_reload_replacement_music(),
            "complete converter JSON loads atomically");
    AsoundDriver driver = {};
    require(asound_start_replacement_music(&driver, 0),
            "replacement intro initializes all voices");
    for (int voice = 0; voice < ASOUND_STREAM_COUNT; ++voice) {
        require(driver.streams[voice].stream_ptr
                && driver.streams[voice].stream_ptr[0] == 100 + voice,
                "intro voice points at its replacement byte stream");
    }
    require(asound_start_replacement_music(&driver, 1),
            "replacement release initializes all voices");
    for (int voice = 0; voice < ASOUND_STREAM_COUNT; ++voice) {
        require(driver.streams[voice].stream_ptr
                && driver.streams[voice].stream_ptr[0] == 200 + voice,
                "release voice points at its replacement byte stream");
    }

    writeMusic(musicPath, false);
    require(!asound_reload_replacement_music(),
            "incomplete music JSON is rejected as a unit");
    require(!asound_start_replacement_music(&driver, 0),
            "rejected music leaves compiled streams as fallback");

    {
        std::ofstream out(musicPath);
        out << "{\"streams\":["
               "{\"source_symbol\":\"asound_intro_voice0\"},"
               "{\"source_symbol\":\"asound_intro_voice1\","
               "\"stream_bytes\":[101,0]}]}";
    }
    require(!asound_reload_replacement_music(),
            "a missing stream cannot borrow bytes from the next object");

    setReplacementRoot("");
    std::filesystem::remove_all(root);
    std::cout << "asound_music_replacement_behavior_tests passed\n";
    return 0;
}
