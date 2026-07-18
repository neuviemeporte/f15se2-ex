#include "shared/blackbox.h"
#include "shared/blackbox_cli.h"
#include "shared/blackbox_diag.h"
#include "shared/blackbox_gl.h"
#include "shared/blackbox_snapshot.h"
#include "shared/blackbox_state.h"
#include "egdata.h"
#include "strand.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

enum : int {
    kFailure = 1,
    kSeed = 7,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kFailure);
    }
}

void writeFile(const std::filesystem::path &path, const std::string &contents) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    require(file.good(), "test fixture file can be created");
    file << contents;
}

std::string validLog(const std::string &events = {}) {
    return "F15SE2_BLACKBOX 7\nseed 7\nbuild_version test-build\n"
           "mutable_file HallFame 0 -\n" + events;
}

void testCli(const std::filesystem::path &recordPath) {
    BlackboxCliOptions options;
    char program[] = "f15se2-ex";
    char debugOption[] = "--blackbox-debug";
    char ignoreOption[] = "--blackbox-replay-ignore-build";
    char replayOption[] = "--blackbox-replay";
    char pauseOption[] = "--blackbox-pause-tick";
    char unknownOption[] = "--not-blackbox";
    char overflow[] = "4294967296";
    char validTime[] = "10001";
    char *singleArgv[] = {program, debugOption};
    int index = 1;

    blackbox_cliInit(&options);
    require(blackbox_cliParseOption(&options, 2, singleArgv, &index) == 1 && options.debug,
            "CLI parses debug mode");
    singleArgv[1] = ignoreOption;
    index = 1;
    require(blackbox_cliParseOption(&options, 2, singleArgv, &index) == 1 &&
                options.ignoreBuild,
            "CLI parses build-mismatch override");
    singleArgv[1] = unknownOption;
    index = 1;
    require(blackbox_cliParseOption(&options, 2, singleArgv, &index) == 0,
            "CLI leaves unrelated options to the main parser");

    char replayPath[] = "replay.bb";
    char *replayArgv[] = {program, replayOption, replayPath};
    index = 1;
    require(blackbox_cliParseOption(&options, 3, replayArgv, &index) == 1 &&
                options.replayPath == replayPath,
            "CLI parses and consumes a replay path");

    char *missingArgv[] = {program, replayOption};
    index = 1;
    require(blackbox_cliParseOption(&options, 2, missingArgv, &index) == -1,
            "CLI rejects a missing path argument");
    char *overflowArgv[] = {program, pauseOption, overflow};
    index = 1;
    require(blackbox_cliParseOption(&options, 3, overflowArgv, &index) == -1,
            "CLI rejects values outside uint32 range");
    char *pauseArgv[] = {program, pauseOption, validTime};
    index = 1;
    require(blackbox_cliParseOption(&options, 3, pauseArgv, &index) == 1 &&
                options.pauseTick == 6001,
            "CLI parses pause time");
    blackbox_cliPrintUsage();

    blackbox_cliInit(&options);
    options.recordPath = "record.bb";
    options.replayPath = "replay.bb";
    require(!blackbox_cliStart(&options), "CLI rejects simultaneous record and replay");

    blackbox_cliInit(&options);
    options.fastForwardTick = 10;
    require(!blackbox_cliStart(&options), "CLI requires replay for fast-forward");

    blackbox_cliInit(&options);
    options.captureRender = 1;
    require(!blackbox_cliStart(&options), "CLI requires recording for render capture");

    blackbox_cliInit(&options);
    options.dumpTick = 10;
    require(!blackbox_cliStart(&options), "CLI requires a blackbox mode for dumps");

    blackbox_cliInit(&options);
    options.replayPath = "missing.bb";
    options.fastForwardTick = 20;
    options.pauseTick = 10;
    require(!blackbox_cliStart(&options), "CLI rejects pause before fast-forward");

    blackbox_cliInit(&options);
    options.debug = 1;
    options.dumpTick = 20;
    options.pauseTick = 10;
    require(!blackbox_cliStart(&options), "CLI rejects pause before automatic dump");

    blackbox_cliInit(&options);
    options.debug = 1;
    options.pauseTick = 12;
    options.dumpTick = 11;
    require(blackbox_cliStart(&options), "CLI starts debug mode with valid triggers");
    require(blackbox_enabled(), "successful CLI start enables blackbox mode");
    blackbox_shutdown();

    const std::string recordString = recordPath.string();
    blackbox_cliInit(&options);
    options.recordPath = recordString.c_str();
    options.captureRender = 1;
    require(blackbox_cliStart(&options), "CLI starts record mode with render capture");
    blackbox_shutdown();

    blackbox_cliInit(&options);
    require(blackbox_cliStart(&options) && !blackbox_enabled(),
            "CLI accepts normal mode without enabling blackbox");

    writeFile(recordPath, validLog());
    blackbox_cliInit(&options);
    options.replayPath = recordString.c_str();
    options.fastForwardTick = 10;
    options.ignoreBuild = 1;
    require(blackbox_cliStart(&options) && blackbox_fastForwarding(),
            "CLI starts replay and applies its fast-forward target");
    blackbox_shutdown();
}

void testState(const std::filesystem::path &path) {
    const uint8 bytes[] = {0xab, 0xcd};
    const uint8 *replayData = nullptr;
    uint32 replaySize = 0;

    blackbox_state_reset();
    blackbox_state_setBuildVersion(nullptr);
    require(std::string(blackbox_state_currentBuildVersion()) == "unknown" &&
                std::string(blackbox_state_replayBuildVersion()).empty(),
            "state metadata has safe defaults");
    blackbox_state_writeRecordHeader(nullptr);
    require(!blackbox_state_shouldCaptureMutableFile(nullptr) &&
                !blackbox_state_shouldCaptureMutableFile("Other") &&
                blackbox_state_shouldCaptureMutableFile("hallfame"),
            "only HallFame is mutable blackbox state");
    require(!blackbox_state_getReplayMutableFile("HallFame", &replayData, &replaySize),
            "no replay state exists before parsing");
    require(!blackbox_state_validateReplay(), "state validation requires build metadata");

    FILE *file = std::fopen(path.string().c_str(), "wb");
    require(file != nullptr, "state record fixture opens");
    blackbox_state_setBuildVersion("build-a");
    blackbox_state_writeRecordHeader(file);
    blackbox_state_recordMutableFile(nullptr, "HallFame", bytes, 2);
    blackbox_state_recordMutableFile(file, nullptr, bytes, 2);
    blackbox_state_recordMutableFile(file, "HallFame", nullptr, 2);
    blackbox_state_recordMutableFile(file, "HallFame", bytes, 2);
    blackbox_state_recordMutableFile(file, "HallFame", bytes, 2);
    std::fclose(file);
    require(!blackbox_state_shouldCaptureMutableFile("HallFame"),
            "mutable state is recorded only once");

    blackbox_state_reset();
    require(blackbox_state_parseReplayLine("unrelated line\n") == 0,
            "state parser ignores non-state lines");
    require(blackbox_state_parseReplayLine("mutable_file Other 0 -\n") < 0,
            "state parser rejects unknown mutable files");
    require(blackbox_state_parseReplayLine("mutable_file HallFame 0 00\n") < 0,
            "empty snapshots require the explicit marker");
    require(blackbox_state_parseReplayLine("mutable_file HallFame 2 ab\n") < 0,
            "state parser rejects truncated hex payloads");
    require(blackbox_state_parseReplayLine("mutable_file HallFame 1 zz\n") < 0,
            "state parser rejects non-hex payloads");
    require(blackbox_state_parseReplayLine("build_version build-b\n") == 1 &&
                blackbox_state_parseReplayLine("mutable_file hallfame 2 ABCD\n") == 1,
            "state parser accepts case-insensitive names and uppercase hex");
    require(blackbox_state_getReplayMutableFile("HALLFAME", &replayData, &replaySize) &&
                replaySize == 2 && replayData[0] == 0xab && replayData[1] == 0xcd,
            "parsed mutable bytes are available to replay");
    blackbox_state_setBuildVersion("build-a");
    blackbox_state_setAllowBuildMismatch(0);
    require(!blackbox_state_validateReplay(), "build mismatch is rejected by default");
    blackbox_state_setAllowBuildMismatch(1);
    require(blackbox_state_validateReplay(), "explicit override permits build mismatch");
    blackbox_state_reset();
    require(blackbox_state_parseReplayLine("build_version build-a\n") == 1 &&
                !blackbox_state_validateReplay(),
            "state validation requires a HallFame snapshot");
    blackbox_state_reset();
}

void testCoreFailures(const std::filesystem::path &path) {
    blackbox_setBuildVersion("test-build");
    blackbox_setAllowBuildMismatch(0);
    require(!blackbox_startRecord("missing-directory/record.bb", kSeed),
            "recording reports an unwritable destination");
    require(!blackbox_startReplay("missing-replay.bb"),
            "replay reports a missing recording");

    writeFile(path, "not a blackbox\n");
    require(!blackbox_startReplay(path.string().c_str()),
            "replay rejects a bad format header");
    writeFile(path, validLog("axes 0 0 0 0 256\n"));
    require(!blackbox_startReplay(path.string().c_str()),
            "replay rejects out-of-range axes");
    writeFile(path, validLog("unknown 1 2 3\n"));
    require(!blackbox_startReplay(path.string().c_str()),
            "replay rejects unknown event lines");
    writeFile(path, "F15SE2_BLACKBOX 7\nseed 7\nmutable_file HallFame 0 -\n");
    require(!blackbox_startReplay(path.string().c_str()),
            "replay requires build metadata");
    writeFile(path, "F15SE2_BLACKBOX 7\nseed 7\nbuild_version test-build\n");
    require(!blackbox_startReplay(path.string().c_str()),
            "replay requires captured mutable state");

    writeFile(path, validLog("phase 0 start\naxes 0 1 2 3 4\n"));
    require(blackbox_startReplay(path.string().c_str()),
            "replay accepts navigation markers and valid axes");
    blackbox_getDebugState(nullptr);
    blackbox_seedRandom(kSeed);
    blackbox_seedRandom(kSeed);
    blackbox_seedExternalRandom(1234);
    (void)blackbox_rand15();
    SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_INDEX8);
    require(surface != nullptr, "frame exhaustion fixture creates an indexed surface");
    blackbox_recordFrame(surface);
    SDL_DestroySurface(surface);
    blackbox_shutdown();

    require(blackbox_startDebug(kSeed), "debug mode starts for fallback RNG checks");
    require(blackbox_seedExternalRandom(9999) == kSeed,
            "debug mode ignores external wall-clock seeds");
    blackbox_seedConfiguredRandom();
    require(blackbox_timerNowNs() == 0 && blackbox_randomActive(),
            "debug mode exposes deterministic clock and RNG state");
    blackbox_logPhase(nullptr);
    blackbox_recordKey(1);
    blackbox_recordAxes(1, 2, 3, 4);
    uint8 x = 1, y = 2, joyX = 3, joyY = 4;
    blackbox_applyReplayAxes(&x, &y, &joyX, &joyY);
    blackbox_drawGlOverlay(nullptr, 0, nullptr, nullptr, nullptr);
    blackbox_shutdown();

    gameSrand(kSeed);
    const int first = gameRand();
    gameSrand(kSeed);
    require(first == gameRand() && gameRand15() >= 0,
            "normal game RNG remains deterministic outside blackbox mode");
}

void testDiagnosticTransitions(const std::filesystem::path &path) {
    const ViewMode savedViewMode = g_viewMode;
    const int16 savedTrackedEnemy = g_trackedEnemyIdx;
    const int16 savedAirLock = g_airTargetLock;
    const int16 savedGroundLock = g_groundTargetLock;
    const int16 savedMissionStatus = g_missionStatus;
    const uint8 savedMissionEnded = g_missionEndedFlag[0];
    const Projectile savedProjectile = g_projectiles[0];

    require(blackbox_startRecord(path.string().c_str(), kSeed),
            "diagnostic transition fixture starts recording");
    blackbox_diagCaptureSimStep();

    g_viewMode = static_cast<ViewMode>(static_cast<int>(savedViewMode) + 1);
    g_trackedEnemyIdx = 3;
    g_airTargetLock = 4;
    g_groundTargetLock = 5;
    g_projectiles[0].ttl = 10;
    g_projectiles[0].weaponIdx = 2;
    g_projectiles[0].targetLock = 3;
    blackbox_diagCaptureSimStep();

    g_projectiles[0].ttl = 0;
    g_missionStatus = 9;
    g_missionEndedFlag[0] = 1;
    blackbox_diagCaptureSimStep();
    blackbox_shutdown();

    std::ifstream recorded(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(recorded)),
                           std::istreambuf_iterator<char>());
    require(text.find(" view_change ") != std::string::npos &&
                text.find(" target_change ") != std::string::npos &&
                text.find(" projectile_launch ") != std::string::npos &&
                text.find(" projectile_remove ") != std::string::npos &&
                text.find(" mission_end ") != std::string::npos,
            "diagnostics record all important gameplay transitions");

    g_viewMode = savedViewMode;
    g_trackedEnemyIdx = savedTrackedEnemy;
    g_airTargetLock = savedAirLock;
    g_groundTargetLock = savedGroundLock;
    g_missionStatus = savedMissionStatus;
    g_missionEndedFlag[0] = savedMissionEnded;
    g_projectiles[0] = savedProjectile;
}

void testRngSnapshotsAndDivergence(const std::filesystem::path &dir) {
    const auto streamsPath = dir / "streams.bb";
    const auto recordPath = dir / "coverage-record.bb";
    const auto snapshotPath = dir / "snapshot.json";
    const auto dumpPath = dir / "render-dump.txt";

    writeFile(streamsPath, validLog(
        "rng_seed 9 1234\nrng 9 2468\nframe 9 1 deadbeef\nkey 9 9 1234\n"));
    require(blackbox_startReplay(streamsPath.string().c_str()),
            "replay accepts complete deterministic event streams");
    blackbox_shutdown();

    require(blackbox_startReplay(streamsPath.string().c_str()),
            "external-seed divergence fixture starts replay");
    require(blackbox_seedExternalRandom(9999) == 1234,
            "replay restores an external seed despite a tick mismatch");
    (void)blackbox_seedExternalRandom(9999);
    blackbox_shutdown();

    require(blackbox_startRecord(recordPath.string().c_str(), kSeed),
            "recording starts for duplicate-axis suppression");
    blackbox_recordAxes(1, 2, 3, 4);
    blackbox_recordAxes(1, 2, 3, 4);
    blackbox_shutdown();

    seedRandom();
    int16 clockSeed = 0;
    gameSrandFromClock(&clockSeed);
    require(blackbox_startDebug(kSeed), "debug starts for RNG wrapper coverage");
    seedRandom();
    gameSrandFromClock(&clockSeed);
    (void)gameRand();
    blackbox_setBuildVersion("build-\"\\\n");
    require(blackbox_snapshotWriteJson(snapshotPath.string().c_str()),
            "debug snapshot escapes build metadata as valid JSON");
    blackbox_shutdown();

    require(blackbox_startRecord(recordPath.string().c_str(), kSeed),
            "record mode starts for snapshot and clock-seed coverage");
    gameSrandFromClock(&clockSeed);
    require(blackbox_snapshotWriteJson(snapshotPath.string().c_str()),
            "record-mode state can be serialized");
    blackbox_shutdown();

    blackbox_setBuildVersion("test-build");
    writeFile(streamsPath, validLog());
    require(blackbox_startReplay(streamsPath.string().c_str()),
            "replay mode starts for snapshot coverage");
    require(blackbox_snapshotWriteJson(snapshotPath.string().c_str()),
            "replay-mode state can be serialized");
    blackbox_shutdown();

    writeFile(streamsPath, validLog(
        "marker 9 expected 1 2 3\n"
        "state 9 9 expected deadbeef\n"
        "render_hash 9 9 9 9 9 deadbeef\n"));
    require(blackbox_startReplay(streamsPath.string().c_str()),
            "diagnostic stream fixture starts replay");
    blackbox_shutdown();
    require(blackbox_startReplay(streamsPath.string().c_str()),
            "diagnostic divergence fixture restarts replay");
    blackbox_diagMarker("actual", 4, 5, 6);
    blackbox_diagCaptureSimStep();
    R3DScene scene = {nullptr, 1, 2, 3, 4, 5, 6, 1};
    blackbox_diagBeginRenderFrame();
    blackbox_diagRenderBeginScene(&scene);
    blackbox_diagRenderEndScene();
    blackbox_shutdown();

    require(blackbox_startDebug(kSeed), "debug starts for render-command diagnostics");
    R3DSubmit submit = {g_world3dData, 1, 2, 3, 4, 5, 6, 0};
    R3DLine line = {1, 2, 3, 4, 5, 6, 15};
    blackbox_diagBeginRenderFrame();
    blackbox_diagRenderBeginScene(&scene);
    for (int i = 0; i < 300; ++i) blackbox_diagRenderSubmit(&submit);
    blackbox_diagRenderLine(&line);
    require(blackbox_diagWriteDump(dumpPath.string().c_str()),
            "diagnostic dump includes retained and dropped render commands");
    blackbox_shutdown();
}

} // namespace

int main() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "f15se2-blackbox-validation";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto recordPath = dir / "record.bb";
    const auto statePath = dir / "state.txt";
    const auto replayPath = dir / "replay.bb";

    testCli(recordPath);
    testState(statePath);
    testCoreFailures(replayPath);
    testDiagnosticTransitions(dir / "diagnostics.bb");
    testRngSnapshotsAndDivergence(dir);

    blackbox_shutdown();
    std::filesystem::remove_all(dir);
    std::cout << "blackbox_validation_tests passed\n";
    return 0;
}
