#include "shared/blackbox.h"
#include "shared/blackbox_cli.h"
#include "shared/blackbox_diag.h"
#include "shared/blackbox_wait.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

enum BlackboxTestConstant : int {
    kSeed = BLACKBOX_DEFAULT_SEED,
    kOtherSeed = 4321,
    kBiosWord = 0x1f73,
    kRawX = 0x40,
    kRawY = 0xc0,
    kJoyX = 0x30,
    kJoyY = 0xd0,
    kTestFailureExitCode = 1,
};

const unsigned char kHallFameSnapshot[] = {'H', 'A', 'L', 'L'};
int gVirtualWaitPolls = 0;

bool pollVirtualWait() {
    gVirtualWaitPolls++;
    blackbox_noteTick();
    return gVirtualWaitPolls >= 3;
}

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

std::string tempLogPath() {
    static unsigned sequence = 0;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("f15-blackbox-test-" + std::to_string(SDL_GetTicksNS()) + "-" +
         std::to_string(sequence++));
    FILE *file = std::fopen(path.string().c_str(), "wb");
    if (file) std::fclose(file);
    return path.string();
}

} // namespace

int main() {
    const std::string path = tempLogPath();
    const std::string badPath = tempLogPath();

    {
        BlackboxCliOptions options;
        char program[] = "f15se2-ex";
        char recordOption[] = "--blackbox-record";
        char recordPath[] = "/tmp/test.bb";
        char *recordArgv[] = {program, recordOption, recordPath};
        int index = 1;
        blackbox_cliInit(&options);
        require(options.seed == BLACKBOX_DEFAULT_SEED,
                "blackbox CLI uses the documented default seed");
        require(blackbox_cliParseOption(&options, 3, recordArgv, &index) == 1 &&
                    options.recordPath == recordPath && index == 2,
                "blackbox CLI parses and consumes a record path");

        char seedOption[] = "--blackbox-seed";
        char invalidSeed[] = "not-a-number";
        char *seedArgv[] = {program, seedOption, invalidSeed};
        index = 1;
        require(blackbox_cliParseOption(&options, 3, seedArgv, &index) == -1,
                "blackbox CLI rejects malformed numeric values");

        char negativeSeed[] = "-1";
        seedArgv[2] = negativeSeed;
        index = 1;
        require(blackbox_cliParseOption(&options, 3, seedArgv, &index) == -1,
                "blackbox CLI rejects negative unsigned values portably");

        char fastForwardOption[] = "--blackbox-fast-forward-tick";
        char fastForwardTick[] = "20545";
        char *fastForwardArgv[] = {program, fastForwardOption, fastForwardTick};
        index = 1;
        require(blackbox_cliParseOption(&options, 3, fastForwardArgv, &index) == 1 &&
                    options.fastForwardTick == 12345 && index == 2,
                "blackbox CLI converts displayed time to its internal tick");

        char dumpOption[] = "--blackbox-dump-tick";
        char dumpTick[] = "20545";
        char *dumpArgv[] = {program, dumpOption, dumpTick};
        index = 1;
        require(blackbox_cliParseOption(&options, 3, dumpArgv, &index) == 1 &&
                    options.dumpTick == 12345 && index == 2,
                "blackbox CLI parses an automatic snapshot time");

        char zeroDumpTick[] = "0";
        dumpArgv[2] = zeroDumpTick;
        index = 1;
        require(blackbox_cliParseOption(&options, 3, dumpArgv, &index) == -1,
                "blackbox CLI rejects a dump before game state can be captured");

        char invalidDisplayedTick[] = "20575";
        fastForwardArgv[2] = invalidDisplayedTick;
        index = 1;
        require(blackbox_cliParseOption(&options, 3, fastForwardArgv, &index) == -1,
                "blackbox CLI rejects impossible displayed subticks");

        char captureRenderOption[] = "--blackbox-capture-render";
        char *captureRenderArgv[] = {program, captureRenderOption};
        index = 1;
        require(blackbox_cliParseOption(&options, 2, captureRenderArgv, &index) == 1 &&
                    options.captureRender != 0,
                "blackbox CLI enables detailed render-command capture");
    }

    require(blackbox_startRecord(path.c_str(), kSeed) != 0,
            "blackbox opens a text record log");
    blackbox_logPhase("test");
    blackbox_noteTick();
    blackbox_noteInputPump();
    blackbox_recordKey(kBiosWord);
    blackbox_diagMarker("test_event", 1, 2, 3);
    blackbox_diagCaptureSimStep();
    blackbox_diagSetRenderCapture(1);
    {
        R3DScene scene = {nullptr, 1, 2, 3, 4, 5, 6, 1};
        R3DSubmit submit = {nullptr, 7, 8, 9, 10, 11, 12, 0};
        R3DLine line = {1, 2, 3, 4, 5, 6, 15};
        blackbox_diagBeginRenderFrame();
        blackbox_diagRenderBeginScene(&scene);
        blackbox_diagRenderSubmit(&submit);
        blackbox_diagRenderLine(&line);
        blackbox_diagRenderEndScene();
    }
    blackbox_recordAxes(kRawX, kRawY, kJoyX, kJoyY);
    blackbox_captureMutableFile("HallFame", kHallFameSnapshot, sizeof(kHallFameSnapshot));
    blackbox_seedRandom(kSeed);
    const int recordedRand = blackbox_rand15();
    blackbox_shutdown();

    {
        FILE *recorded = std::fopen(path.c_str(), "r");
        char line[1024];
        bool marker = false, state = false, scene = false, object = false, renderLine = false;
        require(recorded != nullptr, "diagnostic recording can be inspected as text");
        while (std::fgets(line, sizeof(line), recorded)) {
            marker |= std::strncmp(line, "marker ", 7) == 0;
            state |= std::strncmp(line, "state ", 6) == 0;
            scene |= std::strncmp(line, "render_scene ", 13) == 0;
            object |= std::strncmp(line, "render_object ", 14) == 0;
            renderLine |= std::strncmp(line, "render_line ", 12) == 0;
        }
        std::fclose(recorded);
        require(marker && state && scene && object && renderLine,
                "recording contains markers, subsystem hashes, and detailed render commands");
    }

    require(blackbox_startDebug(kSeed) != 0,
            "debug mode enables virtual waits");
    gVirtualWaitPolls = 0;
    require(blackbox_virtualWait(10, 1, pollVirtualWait) != 0 &&
                blackbox_tick() == 3,
            "virtual wait stops at deterministic input without host-time dependence");
    blackbox_shutdown();

    require(blackbox_startReplay(path.c_str()) != 0,
            "blackbox loads the recorded log for replay");
    require(blackbox_fastForwarding() == 0,
            "ordinary replay presents normally when no fast-forward target was supplied");
    blackbox_setFastForwardTick(1);
    require(blackbox_fastForwarding() != 0,
            "replay fast-forwards before its target tick");
    uint16 word = 0;
    require(blackbox_replayNextKey(&word) == 0,
            "replay does not emit future input before its recorded input poll");
    blackbox_noteTick();
    require(blackbox_fastForwarding() == 0,
            "replay resumes normal presentation at its target tick");
    require(blackbox_replayNextKey(&word) == 0,
            "a matching tick cannot release input before its recorded poll");
    blackbox_noteInputPump();
    require(blackbox_replayNextKey(&word) != 0 && word == kBiosWord,
            "replay emits the recorded BIOS key on the matching input poll");
    blackbox_diagMarker("test_event", 1, 2, 3);
    blackbox_diagMarker("test_event", 1, 2, 3);
    blackbox_diagCaptureSimStep();
    blackbox_diagCaptureSimStep();
    {
        R3DScene scene = {nullptr, 1, 2, 3, 4, 5, 6, 1};
        R3DSubmit submit = {nullptr, 7, 8, 9, 10, 11, 12, 0};
        R3DLine line = {1, 2, 3, 4, 5, 6, 15};
        blackbox_diagBeginRenderFrame();
        blackbox_diagRenderBeginScene(&scene);
        blackbox_diagRenderSubmit(&submit);
        blackbox_diagRenderLine(&line);
        blackbox_diagRenderEndScene();
        blackbox_diagRenderEndScene();
    }
    uint8 rawX = 0x80, rawY = 0x80, joyX = 0x80, joyY = 0x80;
    blackbox_applyReplayAxes(&rawX, &rawY, &joyX, &joyY);
    require(rawX == kRawX && rawY == kRawY && joyX == kJoyX && joyY == kJoyY,
            "replay restores recorded joystick axes for the tick");
    blackbox_seedRandom(kSeed);
    require(blackbox_rand15() == recordedRand,
            "replay follows the recorded deterministic RNG stream");
    blackbox_shutdown();

    require(blackbox_startRecord(badPath.c_str(), kSeed) != 0,
            "test can record stopped-timer menu input");
    blackbox_captureMutableFile("HallFame", kHallFameSnapshot, sizeof(kHallFameSnapshot));
    blackbox_noteInputPump();
    blackbox_recordKey(kBiosWord);
    blackbox_noteInputPump();
    blackbox_noteInputPump();
    blackbox_recordKey(kOtherSeed);
    blackbox_shutdown();
    require(blackbox_startReplay(badPath.c_str()) != 0,
            "test can replay stopped-timer menu input");
    blackbox_noteInputPump();
    require(blackbox_replayNextKey(&word) != 0 && word == kBiosWord,
            "first same-tick key becomes visible at its input poll");
    blackbox_noteInputPump();
    require(blackbox_replayNextKey(&word) == 0,
            "later same-tick key is not prefetched across a buffer clear");
    blackbox_noteInputPump();
    require(blackbox_replayNextKey(&word) != 0 && word == kOtherSeed,
            "later same-tick key becomes visible only at its own input poll");
    blackbox_shutdown();

    require(blackbox_startRecord(badPath.c_str(), kSeed) != 0,
            "test can record an external RNG seed");
    blackbox_captureMutableFile("HallFame", kHallFameSnapshot, sizeof(kHallFameSnapshot));
    require(blackbox_seedExternalRandom(1234) == 1234,
            "recording uses the supplied external seed");
    blackbox_shutdown();
    require(blackbox_startReplay(badPath.c_str()) != 0,
            "test can replay an external RNG seed");
    require(blackbox_seedExternalRandom(9999) == 1234,
            "replay restores an external seed without using the current value");
    blackbox_shutdown();

    {
        FILE *tickMismatchLog = std::fopen(badPath.c_str(), "w");
        require(tickMismatchLog != nullptr, "test can create an RNG replay log");
        std::fputs("F15SE2_BLACKBOX 7\nseed 7\nbuild_version unknown\nmutable_file HallFame 4 48414c4c\nrng_seed 9 4321\nrng 9 2468\n", tickMismatchLog);
        std::fclose(tickMismatchLog);
        require(blackbox_startReplay(badPath.c_str()) != 0,
                "replay accepts a log with captured RNG events");
        blackbox_seedRandom(kSeed);
        require(blackbox_rand15() == 2468,
                "replay returns recorded RNG values even when local tick/seed differ");
        blackbox_shutdown();
    }

    require(blackbox_startDebug(kSeed) != 0, "debug mode enables deterministic RNG");
    blackbox_setPauseTick(2);
    blackbox_noteTick();
    blackbox_noteTick();
    blackbox_noteTick();
    require(blackbox_tick() == 2 && blackbox_pauseReached(),
            "pause tick freezes deterministic time for timeframe inspection");
    blackbox_shutdown();

    {
        const char *textPath = "blackbox-dump-1.txt";
        const char *jsonPath = "blackbox-dump-1.json";
        std::remove(textPath);
        std::remove(jsonPath);
        require(blackbox_startDebug(kSeed) != 0,
                "debug mode supports automatic state snapshots");
        blackbox_diagSetDumpTick(1);
        blackbox_noteTick();
        blackbox_diagOnTick();
        FILE *textDump = std::fopen(textPath, "r");
        FILE *jsonDump = std::fopen(jsonPath, "r");
        char json[32768] = {};
        require(textDump != nullptr && jsonDump != nullptr,
                "automatic dump writes both text and JSON files at its target");
        require(std::fread(json, 1, sizeof(json) - 1, jsonDump) > 0 &&
                    json[0] == '{' &&
                    std::strstr(json, "\"format\":\"f15se2-blackbox-state\"") != nullptr &&
                    std::strstr(json, "\"sim_objects\"") != nullptr &&
                    std::strstr(json, "\"projectiles\"") != nullptr &&
                    std::strstr(json, "\"bullet_tracks\"") != nullptr &&
                    std::strstr(json, "\"provenance\"") != nullptr &&
                    std::strstr(json, "\"cursors\"") != nullptr &&
                    std::strstr(json, "\"timing\"") != nullptr &&
                    std::strstr(json, "\"waypoints\"") != nullptr &&
                    std::strstr(json, "\"target_slots\"") != nullptr &&
                    std::strstr(json, "\"map_targets\"") != nullptr,
                "automatic JSON snapshot has diagnostic context and object collections");
        std::fclose(textDump);
        std::fclose(jsonDump);

        jsonDump = std::fopen(jsonPath, "a");
        require(jsonDump != nullptr, "automatic snapshot fixture can mark its output");
        std::fputs("snapshot-not-overwritten\n", jsonDump);
        std::fclose(jsonDump);
        blackbox_noteTick();
        blackbox_diagOnTick();
        jsonDump = std::fopen(jsonPath, "r");
        std::memset(json, 0, sizeof(json));
        require(jsonDump != nullptr && std::fread(json, 1, sizeof(json) - 1, jsonDump) > 0 &&
                    std::strstr(json, "snapshot-not-overwritten") != nullptr,
                "automatic dump fires only once");
        std::fclose(jsonDump);
        blackbox_shutdown();
        std::remove(textPath);
        std::remove(jsonPath);
    }

    require(blackbox_startDebug(kSeed) != 0, "debug mode can be restarted after pause");
    const int firstA = blackbox_rand15();
    const int secondA = blackbox_rand15();
    blackbox_seedRandom(kSeed);
    require(firstA == blackbox_rand15() && secondA == blackbox_rand15(),
            "deterministic RNG repeats for the same seed");
    blackbox_seedRandom(kOtherSeed);
    require(firstA != blackbox_rand15(),
            "deterministic RNG changes when the seed changes");
    require(blackbox_diagWriteDump(badPath.c_str()) != 0,
            "manual diagnostics write a readable state/render dump");
    {
        FILE *dump = std::fopen(badPath.c_str(), "r");
        char dumpText[4096] = {};
        require(dump != nullptr && std::fread(dumpText, 1, sizeof(dumpText) - 1, dump) > 0,
                "manual diagnostic dump can be read back");
        std::fclose(dump);
        require(std::strstr(dumpText, "subsystem flight=") != nullptr &&
                    std::strstr(dumpText, "object[00]") != nullptr,
                "manual dump contains subsystem hashes and object state");
    }

    SDL_Surface *surface = SDL_CreateSurface(64, 16, SDL_PIXELFORMAT_INDEX8);
    require(surface != nullptr, "SDL creates an indexed debug overlay surface");
    std::memset(surface->pixels, 7, static_cast<size_t>(surface->pitch) * surface->h);
    blackbox_recordFrame(surface);
    blackbox_drawDebugOverlay(surface);
    require(static_cast<unsigned char *>(surface->pixels)[0] == 0,
            "debug overlay clears its top-left background");
    blackbox_restoreDebugOverlay(surface);
    require(static_cast<unsigned char *>(surface->pixels)[0] == 7,
            "debug overlay restores the game page after presentation");

    require(blackbox_startRecord(badPath.c_str(), kSeed) != 0,
            "test can record logical frames while excluding passive redraws");
    blackbox_captureMutableFile("HallFame", kHallFameSnapshot, sizeof(kHallFameSnapshot));
    blackbox_beginPassivePresent();
    blackbox_recordFrame(surface);
    blackbox_endPassivePresent();
    for (int tick = 0; tick < 5; tick++) blackbox_noteTick();
    blackbox_recordFrame(surface);
    blackbox_shutdown();
    require(blackbox_startReplay(badPath.c_str()) != 0,
            "test can replay a logical-frame log");
    blackbox_recordFrame(surface);
    require(blackbox_tick() == 0,
            "frame verification never changes deterministic time");
    blackbox_shutdown();

    SDL_DestroySurface(surface);
    blackbox_shutdown();

    {
        FILE *badLog = std::fopen(badPath.c_str(), "w");
        require(badLog != nullptr, "test can create a malformed replay log");
        std::fputs("F15SE2_BLACKBOX 7\nseed 7\nbuild_version unknown\nmutable_file HallFame 4 48414c4c\nkey 1 1 10000\n", badLog);
        std::fclose(badLog);
        require(blackbox_startReplay(badPath.c_str()) == 0,
                "replay rejects out-of-range BIOS key words instead of truncating them");
    }

    {
        FILE *badLog = std::fopen(badPath.c_str(), "w");
        require(badLog != nullptr, "test can create a malformed snapshot log");
        std::fputs("F15SE2_BLACKBOX 7\nseed 7\nbuild_version unknown\nmutable_file HallFame 1 00ff\n", badLog);
        std::fclose(badLog);
        require(blackbox_startReplay(badPath.c_str()) == 0,
                "replay rejects mutable snapshots with trailing data");
    }

    {
        FILE *largeLog = std::fopen(badPath.c_str(), "w");
        require(largeLog != nullptr, "test can create a multi-capacity replay log");
        std::fputs("F15SE2_BLACKBOX 7\nseed 7\nbuild_version unknown\nmutable_file HallFame 0 -\n", largeLog);
        for (int i = 0; i < 130; i++)
            std::fprintf(largeLog, "key 0 0 %04x\n", i + 1);
        std::fclose(largeLog);
        require(blackbox_startReplay(badPath.c_str()) != 0,
                "replay grows event storage geometrically");
        uint16 replayWord = 0;
        int replayedKeys = 0;
        while (blackbox_replayNextKey(&replayWord)) replayedKeys++;
        require(replayedKeys == 130 && replayWord == 130,
                "grown replay storage preserves every event in order");
        blackbox_shutdown();
    }

    std::remove(path.c_str());
    std::remove(badPath.c_str());
    std::cout << "blackbox_behavior_tests passed\n";
    return 0;
}
