#include "comm.h"
#include "endata.h"
#include "endtypes.h"
#include "gfx.h"
#include "inttype.h"
#include "struct.h"

#include <SDL3/SDL_iostream.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

extern void showPostMissionAwards(void);
extern void loadPicFromFile(const char *name, int segment);
extern void loadPicFromFileAt(const char *name, int segment, int off, SDL_IOWhence whence);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum AwardOriginalConstant : int {
    kCampaignNone = 0,
    kCampaignDeadDesk = 1,
    kCampaignDeath = 2,
    kTrainingMission = 1,
    kRankSecondLieutenant = 0,
    kRankFirstLieutenant = 1,
    kRankGeneral = 6,
    kPromotionFadeSteps = 6,
    kDeskFadeSteps = 3,
    kDeathFadeSteps = 2,
    kMedalFadeSteps = 10,
    kMedalHighestIndex = 4,
    kMedalSilverStarIndex = 2,
    kMedalBitBase = 1,
    kDefaultAwardColor = 7,
    kDefaultAwardFont = 1,
    kUnsetRecorderValue = -1,
    kAwardPageClearX1 = 0,
    kAwardPageClearY1 = 0,
    kAwardPageClearX2 = 319,
    kAwardPageClearY2 = 199,
    kPicOpenModeRead = 0,
    kPicSegmentRaw = 0x1234,
    kPicSegmentSeeked = 0x2345,
    kPicSeekOffset = 3,
    kPicBufferBytes = 8,
    kExpectedOneCall = 1,
};

struct TextCall {
    std::string text;
    int startX;
    int y;
    int endX;
};

std::vector<std::string> g_pics;
std::vector<TextCall> g_textCalls;
std::vector<int> g_fadeSteps;
int g_commitCalls = 0;
int g_flipCalls = 0;
int g_waitKeyCalls = 0;
int g_waitRetraceCalls = 0;
int g_clearCalls = 0;
int g_lastClearX1 = -1;
int g_lastClearY1 = -1;
int g_lastClearX2 = -1;
int g_lastClearY2 = -1;
int g_openWrapperCalls = 0;
int g_closeWrapperCalls = 0;
int g_decodeRawCalls = 0;
int g_decodePicCalls = 0;
int g_lastOpenMode = -1;
int g_lastDecodeSegment = -1;
Sint64 g_lastDecodeOffset = -1;
std::string g_lastOpenName;
unsigned char g_picBuffer[kPicBufferBytes] = {};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void resetAwardState(struct GameComm &comm, struct Game &game) {
    std::memset(&comm, 0, sizeof(comm));
    std::memset(&game, 0, sizeof(game));
    commData = &comm;
    gameData = &game;
    missionScore = 0;
    awardColor = kDefaultAwardColor;
    awardFont = kDefaultAwardFont;
    textBuf[0] = '\0';
    g_pics.clear();
    g_textCalls.clear();
    g_fadeSteps.clear();
    g_commitCalls = 0;
    g_flipCalls = 0;
    g_waitKeyCalls = 0;
    g_waitRetraceCalls = 0;
    g_clearCalls = 0;
    g_lastClearX1 = g_lastClearY1 = g_lastClearX2 = g_lastClearY2 = kUnsetRecorderValue;
    g_openWrapperCalls = 0;
    g_closeWrapperCalls = 0;
    g_decodeRawCalls = 0;
    g_decodePicCalls = 0;
    g_lastOpenMode = kUnsetRecorderValue;
    g_lastDecodeSegment = kUnsetRecorderValue;
    g_lastDecodeOffset = kUnsetRecorderValue;
    g_lastOpenName.clear();
}

bool sawText(const char *needle) {
    for (const TextCall &call : g_textCalls) {
        if (call.text.find(needle) != std::string::npos) return true;
    }
    return false;
}

} // namespace

struct GameComm *commData = nullptr;
struct Game *gameData = nullptr;

void FAR CDECL gfx_setFadeSteps(int steps) { g_fadeSteps.push_back(steps); }
void FAR CDECL gfx_commitPage(void) { ++g_commitCalls; }
void FAR CDECL gfx_flipPage(void) { ++g_flipCalls; }
void FAR CDECL gfx_waitRetrace(void) { ++g_waitRetraceCalls; }

void openShowPic(const char *filename, int) { g_pics.emplace_back(filename); }

SDL_IOStream *openFileWrapper(const char *filename, int mode) {
    ++g_openWrapperCalls;
    g_lastOpenName = filename;
    g_lastOpenMode = mode;
    return SDL_IOFromMem(g_picBuffer, sizeof(g_picBuffer));
}

void closeFileWrapper(SDL_IOStream *handle) {
    ++g_closeWrapperCalls;
    if (handle) SDL_CloseIO(handle);
}

void decodePicRaw(SDL_IOStream *handle, int segment) {
    ++g_decodeRawCalls;
    g_lastDecodeSegment = segment;
    g_lastDecodeOffset = SDL_TellIO(handle);
}

void decodePic(SDL_IOStream *handle, int segment) {
    ++g_decodePicCalls;
    g_lastDecodeSegment = segment;
    g_lastDecodeOffset = SDL_TellIO(handle);
}

void drawStringCentered(int16 *, const char *str, int startX, int y, int endX) {
    g_textCalls.push_back({str, startX, y, endX});
}

void waitForKeyOrJoy(void) { ++g_waitKeyCalls; }

void clearRect(int16 *, int x1, int y1, int x2, int y2) {
    ++g_clearCalls;
    g_lastClearX1 = x1;
    g_lastClearY1 = y1;
    g_lastClearX2 = x2;
    g_lastClearY2 = y2;
}

void mystrcpy(char *dest, const char *source) { std::strcpy(dest, source); }
void mystrcat(char *dest, const char *source) { std::strcat(dest, source); }

int main() {
    struct GameComm comm = {};
    struct Game game = {};

    resetAwardState(comm, game);
    loadPicFromFile("RAW.PIC", kPicSegmentRaw);
    require(g_openWrapperCalls == kExpectedOneCall &&
                g_lastOpenName == "RAW.PIC" &&
                g_lastOpenMode == kPicOpenModeRead &&
                g_decodeRawCalls == kExpectedOneCall &&
                g_decodePicCalls == 0 &&
                g_lastDecodeSegment == kPicSegmentRaw &&
                g_lastDecodeOffset == 0 &&
                g_closeWrapperCalls == kExpectedOneCall,
            "loadPicFromFile preserves the original open/decodePicRaw/close wrapper chain");

    resetAwardState(comm, game);
    loadPicFromFileAt("SEEK.PIC", kPicSegmentSeeked, kPicSeekOffset, SDL_IO_SEEK_SET);
    require(g_openWrapperCalls == kExpectedOneCall &&
                g_lastOpenName == "SEEK.PIC" &&
                g_lastOpenMode == kPicOpenModeRead &&
                g_decodeRawCalls == 0 &&
                g_decodePicCalls == kExpectedOneCall &&
                g_lastDecodeSegment == kPicSegmentSeeked &&
                g_lastDecodeOffset == kPicSeekOffset &&
                g_closeWrapperCalls == kExpectedOneCall,
            "loadPicFromFileAt preserves the original open/seek/decodePic/close wrapper chain");

    resetAwardState(comm, game);
    comm.trainingFlag = kTrainingMission;
    game.rank = kRankSecondLieutenant;
    game.totalScore = promoThresholds[kRankSecondLieutenant] + 1;
    missionScore = medalThresholds[kMedalHighestIndex] + 1;
    showPostMissionAwards();
    require(game.rank == kRankSecondLieutenant && game.medals == 0 &&
                g_pics.empty() && g_commitCalls == 0 && g_waitKeyCalls == 0,
            "showPostMissionAwards skips all awards for original training missions");
    require(g_clearCalls == 1 && g_flipCalls == 1,
            "showPostMissionAwards still clears and flips the award page for training missions");

    resetAwardState(comm, game);
    std::strcpy(game.pilotName, "ACE");
    game.campaignProgress = kCampaignDeadDesk;
    showPostMissionAwards();
    require(g_fadeSteps.size() == 1 && g_fadeSteps[0] == kDeskFadeSteps &&
                g_pics.size() == 1 && g_pics[0] == "desk.pic",
            "showPostMissionAwards shows the original desk-job picture for campaignProgress 1");
    require(sawText("desk job") && sawText("ACE"),
            "showPostMissionAwards writes original desk-job message and pilot name");
    require(awardColor == kDefaultAwardColor && awardFont == kDefaultAwardFont,
            "showPostMissionAwards restores original award color and font after desk-job name");
    require(g_commitCalls == 1 && g_waitKeyCalls == 1,
            "showPostMissionAwards displays exactly one desk-job award page");

    resetAwardState(comm, game);
    game.campaignProgress = kCampaignDeath;
    showPostMissionAwards();
    require(g_fadeSteps.size() == 1 && g_fadeSteps[0] == kDeathFadeSteps &&
                g_pics.size() == 1 && g_pics[0] == "death.pic",
            "showPostMissionAwards shows the original death picture for campaignProgress 2");
    require(sawText("horrible crash") && g_commitCalls == 1 && g_waitKeyCalls == 1,
            "showPostMissionAwards writes and waits on the original death message");

    resetAwardState(comm, game);
    game.campaignProgress = kCampaignNone;
    game.rank = kRankSecondLieutenant;
    game.totalScore = promoThresholds[kRankSecondLieutenant] + 1;
    missionScore = medalThresholds[kMedalSilverStarIndex] + 1;
    showPostMissionAwards();
    require(game.rank == kRankFirstLieutenant,
            "showPostMissionAwards promotes when total score strictly exceeds original threshold");
    require(game.medals == (kMedalBitBase << kMedalSilverStarIndex),
            "showPostMissionAwards awards the highest original medal below mission score");
    require(g_fadeSteps.size() == 2 && g_fadeSteps[0] == kPromotionFadeSteps &&
                g_fadeSteps[1] == kMedalFadeSteps,
            "showPostMissionAwards uses original promotion and medal fade steps");
    require(g_pics.size() == 2 && g_pics[0] == "promo.pic" && g_pics[1] == "medal.pic",
            "showPostMissionAwards shows promotion before medal like the original");
    require(sawText(rankNames[kRankFirstLieutenant]) && sawText(medalNames[kMedalSilverStarIndex]),
            "showPostMissionAwards writes original promotion and medal names");
    require(g_commitCalls == 2 && g_waitKeyCalls == 2 && g_waitRetraceCalls == 1,
            "showPostMissionAwards waits once for each displayed award and retraces before medal");

    resetAwardState(comm, game);
    game.rank = kRankSecondLieutenant;
    game.totalScore = promoThresholds[kRankSecondLieutenant];
    missionScore = medalThresholds[0];
    showPostMissionAwards();
    require(game.rank == kRankSecondLieutenant && game.medals == 0 &&
                g_pics.empty() && g_commitCalls == 0 && g_waitKeyCalls == 0,
            "showPostMissionAwards requires strict greater-than thresholds for promotion and medal");

    resetAwardState(comm, game);
    game.rank = kRankGeneral;
    game.totalScore = promoThresholds[kRankGeneral - 1] + 100000L;
    missionScore = medalThresholds[kMedalHighestIndex] + 1;
    game.medals = kMedalBitBase << kMedalHighestIndex;
    showPostMissionAwards();
    require(game.rank == kRankGeneral && game.medals == (kMedalBitBase << kMedalHighestIndex) &&
                g_pics.empty(),
            "showPostMissionAwards does not promote generals or re-award existing medals");

    require(g_clearCalls == 1 && g_lastClearX1 == kAwardPageClearX1 &&
                g_lastClearY1 == kAwardPageClearY1 &&
                g_lastClearX2 == kAwardPageClearX2 &&
                g_lastClearY2 == kAwardPageClearY2,
            "showPostMissionAwards always clears the original full award page rectangle");

    std::cout << "award_behavior_tests passed\n";
    return 0;
}
