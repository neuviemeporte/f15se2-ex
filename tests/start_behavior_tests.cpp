#include "const.h"
#include "comm.h"
#include "entext.h"
#include "gfx.h"
#include "shared/common.h"
#include "stdata.h"
#include "stgen.h"
#include "stterr.h"
#include "sttypes.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <tuple>
#include <unistd.h>
#include <vector>

extern char *formatGridRef(int16 wx, int16 wy, int16 theater);
extern int lookupGridCell(int16 level, int16 col, int16 row);
extern void gameDataToPilot(struct Pilot *pilot);
extern void pilotToGameData(const uint8 *pilotData);
extern void updateHallfame(void);
extern void loadHallfame(void);
extern void displayPilots(void);
extern void printPilot(int pilotIdx);
extern void blinkPilot(void);
extern void processPilotInput(void);
extern void pilotSelect(int16 needSplash);
extern int getJoyKey(void);
extern int readInputKey(void);
extern void pilotNameInput(int16 *page, int maxLen, int height, int clearHeight, struct Pilot *pilot);
extern void clearKeybuf(void);
extern int joyOrKey(void);
extern void drawLine(const int16 *pageNum, int x1, int y1, int x2, int y2, int color);
extern int pollMenuInput(void);
extern void waitJoyKey(void);
extern void showPic640(const char *filename);
extern void clearBriefing(void);
extern int askRepeatMission(void);
extern void missionDecode(void);
extern void checkDiskA(void);
extern void printMission(void);
extern void missionSelect(void);
extern int missionMenuSelect(const char **names, const char **desc, const char *title, int selection);
extern int findOrPlaceItem(int wx, int wy, int slot);
extern int itemDistance(int idx1, int idx2);
extern void positionUnit(int unit, int loc);
extern void setMoveDstComm7A(const char *filename, const char *mode);
extern void memAppend(void *ptr, int itemsz, int count, SDL_IOStream *unused);
extern void doNothing(SDL_IOStream *handle);
extern void exportWorldToComm(const char *filename);
extern void parseWorld(const char *filename);
extern void runGenerator(void);
extern void missionGenerate(void);
extern void buildTargetLabel(int idx);
extern int approxDistance(int dx, int dy);
extern int calcBearing(int dx, int dy);
extern int clampValue(int val, int lo, int hi);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum StartOriginalConstant : int {
    kLibyaTheater = 0,
    kDesertTheater = 1,
    kMediterraneanTheater = 2,
    kEuropeTheater = 3,
    kCentralEuropeTheater = 4,
    kNorthCapeTheater = 5,
    kPersianGulfTheater = 6,
    kInvalidTheater = 7,
    kWorldGridScale = 20,
    kWorldGridDivisorShift = 10,
    kGridDigitBase = 10,
    kGridYDigitInvert = 9,
    kWrappedStartY = 10,
    kWrappedLineHeight = 5,
    kWrappedFont = 3,
    kTerrainParentCell = 2,
    kTerrainLeafCell = 5,
    kTerrainObjectType = 17,
    kTerrainWorldCoord = 0x1000,
    kTerrainSubtileCenter = 0x800,
    kTerrainLevel2ScaledOffset = 0x1000,
    kTerrainFindInputX = kTerrainWorldCoord >> WORLD_COORD_SHIFT,
    kTerrainFindInputY = 0x8000 - (kTerrainWorldCoord >> WORLD_COORD_SHIFT),
    kTerrainFindResultX = (kTerrainWorldCoord + kTerrainSubtileCenter) >> WORLD_COORD_SHIFT,
    kTerrainFindResultY = 0x8000 - ((kTerrainWorldCoord + kTerrainSubtileCenter) >> WORLD_COORD_SHIFT),
    kTerrainExistingObjectSlot = 4,
    kTerrainPlacementSlot = 1,
    kTerrainPlacementObjectIdx = 0x100 + kTerrainObjectType,
    kRankLowMask = 0x0F,
    kRankHighShift = 6,
    kCampaignProgressDead = 1,
    kCampaignProgressRetired = 2,
    kPilotMedalMask = 0x1F,
    kPilotDeadBit = 0x40,
    kPilotRetiredBit = 0x20,
    kSelectedPilotIndex = 5,
    kSpaceGlyphWidth = 2,
    kDefaultGlyphWidth = 4,
    kLibyaGridOffsetX = 6,
    kLibyaGridOffsetY = 4,
    kNorthCapeGridOffsetX = 3,
    kNorthCapeGridOffsetY = 5,
    kFormatGridBufSize = 5,
    kDrawPageWordCount = 8,
    kDrawPageFontSlot = 6,
    kDrawPageXSlot = 4,
    kDrawPageYSlot = 5,
    kWrappedMaxWidth = 25,
    kWrappedX = 12,
    kWrappedLineCount = 4,
    kCarriageReturnExtraY = 2,
    kGridTopLevelBytes = 17,
    kGridLevel2Bytes = 0x100,
    kGridSubLevelBytes = 0x200,
    kGridLevel4 = 4,
    kGridLevel3 = 3,
    kGridLevel2 = 2,
    kGridLevel1 = 1,
    kGridLevel0 = 0,
    kGridOutOfBoundsLow = -1,
    kGridOutOfBoundsHigh = 0x100,
    kObjectTypeTableBytes = 0x64,
    kTerrainCountTableCount = 5,
    kPilotDifficulty = 3,
    kPilotRankHigh = 2,
    kPilotRankLow = 7,
    kRosterRankHigh = 3,
    kRosterRankLow = 9,
    kRosterDifficulty = 4,
    kSampleObjectSlot = 3,
    kSampleWorldX = 0x2000,
    kSampleWorldY = 0x3000,
    kDesertSampleWorldX = 0x0400,
    kDesertSampleWorldY = 0x0800,
    kNorthCapeSampleWorldX = 0x7fff,
    kNorthCapeSampleWorldY = 0x1234,
    kGridLeafValue = 11,
    kGridLeafCol = 22,
    kGridLeafRow = 23,
    kPilotTotalScoreSample = 0x12345678,
    kPilotLastScoreSample = 0x3456,
    kPilotMedalSample = 0x15,
    kRosterTotalScoreSample = 0x01020304,
    kRosterLastScoreSample = 0x0506,
    kRosterMedalSample = 0x1A,
    kPoisonByte = 0x7f,
    kWritableRoster = 1,
    kReadOnlyOriginalDisk = 0,
    kHallfameWritablePilotIdx = 7,
    kHallfameReadOnlyPilotIdx = 4,
    kHallfameInsertScore = 3500,
    kHallfameTopScore = 9000,
    kHallfameScoreStep = 1000,
    kHallfameLowerScore = 3000,
    kHallfameHigherScore = 4000,
    kHallfameExpectedInsertIdx = 6,
    kHallfameSelectedWriteBytes = 2,
    kHallfameSelectedWriteCount = 1,
    kHallfameSaveWriteCount = 9,
    kHallfameRecordWriteSize = 32,
    kHallfameRecordWriteCount = 1,
    kHallfameCreateAttr = 0,
    kHallfameReadSelectedIdx = 3,
    kHallfameReadBaseScore = 1000,
    kHallfameDisplayPilotIdx = 2,
    kHallfameDisplayPilotScore = 765432,
    kHallfameDisplayPilotLastScore = 1234,
    kHallfameDisplayPilotRank = 2,
    kHallfameDisplayPilotMedals = 0x05,
    kHallfameDisplayTextCalls = 2,
    kHallfameDisplayAllTextCalls = 17,
    kHallfameDisplayAllClearCalls = 8,
    kPilotColumnLeft = 16,
    kPilotTopMargin = 20,
    kPilotRowHeight = 44,
    kPilotEntryWidth = 143,
    kPilotMedalCenterWidth = 144,
    kPilotNameHeight = 8,
    kPilotInputAcceptedIdx = 2,
    kPilotInputPreviousIdx = 3,
    kPilotInputWrappedIdx = 2,
    kPilotRetiredOrDeadMask = 0x60,
    kPilotFirstMedalWidth = 9,
    kPilotThirdMedalWidth = 11,
    kPilotNameInputMaxLen = 3,
    kPilotNameInputHeight = 8,
    kPilotNameInputClearHeight = 8,
    kPilotNameInputBackspace = 8,
    kPilotNameInputFirstChar = 'A',
    kPilotNameInputSecondChar = 'B',
    kPilotNameInputThirdChar = 'C',
    kPilotNameInputScriptCount = 5,
    kPilotNameInputPromptX1 = 15,
    kPilotNameInputPromptY1 = 192,
    kPilotNameInputPromptX2 = 303,
    kPilotNameInputPromptY2 = 197,
    kPilotSelectNeedSplash = 1,
    kPilotSelectPicOpenCount = 3,
    kPilotSelectFadeStepFirst = 4,
    kPilotSelectFadeStepSecond = 7,
    kPilotSelectDac = 1,
    kPilotSelectArmPieceSegment = 0,
    kWarningClearX1 = 0,
    kWarningClearY1 = 0,
    kWarningClearX2 = 319,
    kWarningClearY2 = 199,
    kWarningTextCount = 2,
    kWarningScreenColor = 0x0F,
    kWarningEndColor = 7,
    kExpectedOneCall = 1,
    kExpectedNoCalls = 0,
    kExpectedTwoCalls = 2,
    kChildExitOk = 0,
    kTestFailureExitCode = 1,
    kFakeFileHandle = 0x7100,
    kKeyboardOnly = 0,
    kJoystickEnabled = 1,
    kKeyBufferEmpty = 0,
    kKeyBufferPending = 1,
    kJoystickIdle = 0,
    kJoystickPressed = 1,
    kKeyboardSampleKey = 0x1234,
    kInputScriptCapacity = 32,
    kTwoStepScript = 2,
    kThreeStepScript = 3,
    kDrawLinePage = 2,
    kDrawLineX1 = 11,
    kDrawLineY1 = 12,
    kDrawLineX2 = 21,
    kDrawLineY2 = 22,
    kDrawLineColor = 5,
    kJoystickButtonAxis = 0,
    kJoystickAxisY = 1,
    kClearKeybufCheckCalls = 3,
    kClearKeybufGetKeyCalls = 2,
    kKeyboardScanCodeSample = 0x1234,
    kKeyboardScanCodeLowByte = 0x34,
    kJoystickAxisCenter = 128,
    kJoystickAxisUp = 77,
    kPollMenuKeyboardCheckCalls = 2,
    kPollJoystickAxesCalls = 1,
    kPollMenuRepeatTimerStep = 16, // Original repeat hold clears only after timerCounter is greater than 15.
    kPollMenuRepeatCheckCalls = 3,
    kPollMenuRepeatJoystickReads = 4,
    kPollMenuRepeatPollJoystickCalls = 2,
    kClearBriefingX1 = 113,
    kClearBriefingY1 = 13,
    kClearBriefingX2 = 297,
    kClearBriefingY2 = 126,
    kRepeatPromptColor = 8,
    kRepeatPromptY = 66,
    kRepeatPromptX1 = 113,
    kRepeatPromptX2 = 185,
    kRepeatNoKey = 'n',
    kRepeatYesKey = 'Y',
    kMissionDecodeColor = 7,
    kAnimateArmReadyCounter = 6,
    kPrintMissionCenteredTextCount = 7,
    kPrintMissionDrawCallCount = 3,
    kPrintMissionBaseIdx = 12,
    kPrintMissionPrimaryIdx = 13,
    kPrintMissionSecondaryIdx = 14,
    kPrintMissionBaseObjectName = 4,
    kPrintMissionPrimaryObjectName = 5,
    kPrintMissionSecondaryObjectName = 6,
    kPrintMissionBaseWorldX = 0x2000,
    kPrintMissionBaseWorldY = 0x3000,
    kPrintMissionPrimaryColor = 8,
    kPrintMissionCoordsColor = 3,
    kPrintMissionTakeoffY = 32,
    kPrintMissionPrimaryY = 64,
    kPrintMissionSecondaryY = 96,
    kF15SprOpenMode = 0,
    kFakeOpenFileHandle = 0x7200,
    kShowPic640Dac = 4,
    kShowPic640Page = 0,
    kVideoInterrupt = 0x10,
    kVideoModeSetFunction = 0,
    kVideoMode640x350 = 0x10,
    kMissionMenuSelection = 2,
    kMissionMenuTextCount = 11,
    kMissionMenuItemCount = 5,
    kMissionSelectDifficulty = 1,
    kMissionSelectTheater = 2,
    kMissionSelectMenuCount = 2,
    kMissionSelectKeyBufferChecks = 4,
    kMissionSelectOpenCount = 2,
    kMissionSelectFadeSteps = 0,
    kDiskMissingOpenFailures = 1,
    kScenarioProbeCount = 4,
    kMissionSelectScenarioOpenCount = 6,
    kMissionSelectScenarioMenuCount = 6,
    kMissionSelectScenarioKeyActions = 9,
    kMissionSelectScenarioKeyBufferChecks = kMissionSelectScenarioKeyActions * 2,
    kMissionSelectScenarioCancelMenuCount = 4,
    kMissionSelectScenarioCancelKeyActions = 9,
    kMissionSelectScenarioCancelKeyBufferChecks = kMissionSelectScenarioCancelKeyActions * 2,
    kMissionSelectScenarioCancelOpenCount = 7,
    kMissionSelectNoScenarioKeyActions = 5,
    // Four menu actions use two key-buffer checks each; waitJoyKey uses one.
    kMissionSelectNoScenarioKeyBufferChecks = 9,
    kMissionSelectNoScenarioOpenCount = 7,
    kPrintMissionDelayedKeyBufferChecks = 2,
    kPrintMissionTimedStep = PRINTMISS_TIMESTEP,
    kStgenObjectA = 8,
    kStgenObjectB = 9,
    kStgenObjectAX = 1000,
    kStgenObjectAY = 2000,
    kStgenObjectBX = 1300,
    kStgenObjectBY = 2600,
    kStgenDistance = 750,
    kStgenApproxCapInput = 40000,
    kStgenApproxCap = 0x7fff,
    kStgenBearingNorth = 0,
    kStgenBearingEast = 0x4000,
    kStgenBearingSouth = 0x8000,
    kStgenBearingWest = 0xc000,
    kStgenBearingNortheastApprox = 0x204d,
    kStgenBearingSoutheastApprox = 0x5fb3,
    kStgenBearingSouthwestApprox = 0xa04d,
    kStgenBearingNorthwestApprox = 0xdfb3,
    kStgenBearingShallowNortheastApprox = 0x2d1a,
    kStgenBearingShallowSoutheastApprox = 0x52e6,
    kStgenBearingShallowSouthwestApprox = 0xad1a,
    kStgenBearingShallowNorthwestApprox = 0xd2e6,
    kStgenClampLo = -10,
    kStgenClampHi = 10,
    kStgenClampWrapFloor = -0x4000,
    kStgenUnitSlot = 2,
    kStgenPlaneType = 3,
    kStgenUnitXOffset = 9,
    kStgenUnitYOffset = -12,
    kStgenWorldShift = 5,
    kStgenGroundAltitude = 12,
    kStgenCarrierAltitude = 140,
    kStgenUnitHeading = 0xfc00,
    kStgenUnitFlags = 0x403,
    kStgenAppendItemSize = 2,
    kStgenAppendCount = 3,
    kStgenAppendBytes = 6,
    kStgenExportReadItemSize = 2,
    kStgenExportGroundUnitCount = 1,
    kStgenExportWorldObjectCount = 2,
    kStgenExportFlightUnitCount = 1,
    kStgenExportSignatureLow = 0xAA,
    kStgenExportSignatureHigh = 0x55,
    kStgenExportSignatureWord = 0x55AA,
    kStgenExportObjectX = 0x1111,
    kStgenExportObjectY = 0x2222,
    kStgenExportMissionDist = 0x3333,
    kStgenExportEscortFlag = 0x4444,
    kStgenExportMidX = 0x5555,
    kStgenExportMidY = 0x6666,
    kStgenExportTargetX = 0x7777,
    kStgenExportTargetY = 0x1234,
    kStgenExportTarget2X = 0x2345,
    kStgenExportTarget2Y = 0x3456,
    kStgenExportBase2X = 0x4567,
    kStgenExportBase2Y = 0x5678,
    kStgenExportHeaderBytes = 8,
    kStgenExportCountWordBytes = 2,
    kStgenExportWorldObjectBytes = 16,
    kStgenExportFlightUnitBytesPerItem = 36,
    kStgenExportSmallTableBytes = 100,
    kStgenExportTerrainBytes = 0x100,
    kStgenExportMissionFieldBytes = 20,
    kStgenExportTargetBytesPerItem = 18,
    kStgenExportTargetCount = 2,
    kStgenWorldObjectXOffset = 2,
    kStgenWorldObjectYOffset = 4,
    kStgenExportObjectsOffset = kStgenExportHeaderBytes,
    kStgenExportObjectBytes = kStgenExportWorldObjectBytes * kStgenExportReadItemSize,
    kStgenExportFlightCountOffset = kStgenExportObjectsOffset + kStgenExportObjectBytes,
    kStgenExportFlightUnitsOffset = kStgenExportFlightCountOffset + kStgenExportCountWordBytes,
    kStgenExportFlightUnitBytes = kStgenExportFlightUnitBytesPerItem * kStgenExportFlightUnitCount,
    kStgenExportBuf7Offset = kStgenExportFlightUnitsOffset + kStgenExportFlightUnitBytes,
    kStgenExportBuf8Offset = kStgenExportBuf7Offset + kStgenExportSmallTableBytes,
    kStgenExportNamesOffset = kStgenExportBuf8Offset + kStgenExportSmallTableBytes,
    kStgenExportTerrainOffset = kStgenExportNamesOffset + WORLD_BUFSZ,
    kStgenExportMissionFieldsOffset = kStgenExportTerrainOffset + kStgenExportTerrainBytes,
    kStgenExportTargetStructOffset = kStgenExportMissionFieldsOffset + kStgenExportMissionFieldBytes,
    kStgenExportTotalBytes = kStgenExportTargetStructOffset + kStgenExportTargetBytesPerItem * kStgenExportTargetCount,
    kStgenTargetLabelIdx = 10,
    kStgenTargetObjectName = 1,
    kStgenTargetUnitName = 2,
    kStgenLongLabelIdx = 11,
    kStgenLongObjectName = 3,
    kParseWorldReadItemSize = 2,
    kParseWorldGroundUnitCount = 1,
    kParseWorldWorldObjectCount = 2,
    kParseWorldFlightUnitCount = 1,
    kParseWorldSignatureLow = 0x5A,
    kParseWorldSignatureHigh = 0xA5,
    kParseWorldObject0UnitRef = 1,
    kParseWorldObject0X = 0x1357,
    kParseWorldObject0Y = 0x2468,
    kParseWorldObject0Type = 21,
    kParseWorldObject0Flags = 0x0500,
    kParseWorldObject0ObjectIdx = 4,
    kParseWorldFlightWaypoint = 7,
    kParseWorldFlightX = 0x1111,
    kParseWorldFlightY = 0x2222,
    kParseWorldFlightPlaneType = 3,
    kParseWorldBuf7Byte = 0x71,
    kParseWorldBuf8Byte = 0x82,
    kParseWorldObjectTypeByte = 0x93,
    kParseWorldTerrainByte = 0xA4,
    kRunGeneratorMissionPick = 6,
    kRunGeneratorAltCoordsMissionPick = 2,
    kRunGeneratorOffsetSecondTargetMissionPick = 7,
    kRunGeneratorTerrainObjectType = 2,
    kRunGeneratorTerrainObjectIdx = 0x100 + kRunGeneratorTerrainObjectType,
    kRunGeneratorReadItemSize = 5,
    kRunGeneratorWorldObjectCapacity = 0x4B,
    kRunGeneratorFlightUnitCapacity = 0x13,
    kRunGeneratorWorldObjectCount = 3,
    kRunGeneratorGroundUnitCount = 0,
    kRunGeneratorFlightUnitCount = 4,
    kRunGeneratorRandomMissionPick = -1,
    kRunGeneratorPrimaryTargetSlot = 1,
    kRunGeneratorSecondaryTargetSlot = 2,
    kRunGeneratorPrimaryBaseSlot = 3,
    kRunGeneratorSecondaryBaseSlot = 4,
    kRunGeneratorBaseFlags = 0x501,
    kRunGeneratorPrimaryBaseX = 0x3000,
    kRunGeneratorPrimaryBaseY = 0x3000,
    kRunGeneratorSecondaryBaseX = 0x3400,
    kRunGeneratorSecondaryBaseY = 0x3400,
    kRunGeneratorBaseUnitRef = 1,
    kRunGeneratorTerrainCell = 0,
    kRunGeneratorTerrainTileCount = 1,
    kRunGeneratorDefaultWeapon0 = 0,
    kRunGeneratorDefaultWeapon1 = 3,
    kRunGeneratorDefaultWeapon2 = 5,
    kRunGeneratorHistoricalWeapon2 = 6,
    kRunGeneratorRichReadItemSize = 12,
    kRunGeneratorRichWorldObjectCount = 8,
    kRunGeneratorRichGroundUnitCount = 3,
    kRunGeneratorRichFlightUnitCount = 6,
    kRunGeneratorFlightPatrolFlags = 0x80,
    kRunGeneratorFlightWideRangeFlag = 0x40,
    kRunGeneratorPatrolCandidateFlags = 0x001,
    kRunGeneratorTerrainObjectFlag = 0x10,
    kRunGeneratorGroundRemapType = 5,
    kRunGeneratorSpecialUnitType = 21,
    kRunGeneratorPositiveObjectFlagTension = 3,
    kRunGeneratorSnapMissionTension = 1,
    kRunGeneratorEscortMissionTension = 0x15,
    kRunGeneratorLandingMissionType = 4,
    kRunGeneratorNonDsTheater = 0,
    kRunGeneratorHistoricalTargetCount = 8,
    kRunGeneratorObjectIdxStride = 0x80,
    kRunGeneratorFarBaseCoord = 0x7000,
    kRunGeneratorObjectFlagMissionCodeMask = 0xff00,
    kRunGeneratorSnapMissionCodeBit = 0x10,
    kRunGeneratorGridCellMask = 0x3ff,
    kRunGeneratorGridCellCenter = 0x200,
    kRunGeneratorSeedSearchLimit = 64,
    kTerrainMagic = 0x3131,
    kGridMagic = 0x3232,
    kMissionGenerateExpectedOpenCount = 3,
    kMissionGenerateSavedTheaterMask = 3,
};

struct DrawCall {
    int x;
    int y;
    std::string text;
};

struct ScriptedFile {
    std::string name;
    std::vector<uint8> bytes;
    size_t offset = 0;
};

std::vector<DrawCall> g_drawCalls;
std::vector<ScriptedFile> g_namedScriptedFiles;
std::vector<std::string> g_nullOpenNames;
int g_doFcbSearchResult = kWritableRoster;
int g_createCalls = 0;
int g_fileWriteCalls = 0;
int g_fileReadCalls = 0;
int g_fileCloseCalls = 0;
int g_openFileCalls = 0;
int g_openFileNullResponses = 0;
std::vector<uint8> g_scriptedFileBytes;
size_t g_scriptedFileOffset = 0;
int g_clearCalls = 0;
int g_commitCalls = 0;
int g_flipCalls = 0;
int g_waitRetraceCalls = 0;
int g_getKeyCalls = 0;
int g_lastCreateAttr = -1;
int g_lastOpenMode = -1;
int g_lastClearX1 = -1;
int g_lastClearY1 = -1;
int g_lastClearX2 = -1;
int g_lastClearY2 = -1;
std::vector<std::string> g_centeredText;
std::string g_lastOpenName;
int g_activeScriptedFile = -1;
int g_keyBufferScript[kInputScriptCapacity] = {};
int g_keyBufferScriptLen = 0;
int g_keyBufferScriptPos = 0;
int g_getKeyScript[kInputScriptCapacity] = {};
int g_getKeyScriptLen = 0;
int g_getKeyScriptPos = 0;
int g_joystickScript[kInputScriptCapacity] = {};
int g_joystickScriptLen = 0;
int g_joystickScriptPos = 0;
int g_getKeyResult = kKeyboardSampleKey;
int g_checkKeyCalls = 0;
int g_joystickCalls = 0;
int g_cleanupCalls = 0;
int g_restoreCbreakCalls = 0;
int g_setPageNCalls = 0;
int g_lastSetPageN = -1;
int g_setColorCalls = 0;
int g_lastSetColor = -1;
int g_lineWrapperCalls = 0;
int g_nop23Calls = 0;
int g_pollJoystickCalls = 0;
int g_switchColorCalls = 0;
int g_lastSwitchX1 = -1;
int g_lastSwitchY1 = -1;
int g_lastSwitchX2 = -1;
int g_lastSwitchY2 = -1;
int g_lastSwitchFrom = -1;
int g_lastSwitchTo = -1;
int g_showSpriteCalls = 0;
int g_lastSpriteX = -1;
int g_lastSpriteY = -1;
int g_lastSpriteSrcX = -1;
int g_lastSpriteSrcY = -1;
int g_lastSpriteWidth = -1;
int g_lastSpriteHeight = -1;
int g_copyRectCalls = 0;
int g_blitToCurrentCalls = 0;
int g_timerYieldCalls = 0;
int g_setTimerCalls = 0;
int g_restoreTimerCalls = 0;
int g_timerCounterStepOnKeyCheck = 0;
int g_polledJoyX = kJoystickAxisCenter;
int g_polledJoyY = kJoystickAxisCenter;
int g_intDispatchCalls = 0;
int g_lastInterrupt = -1;
int g_setDacCalls = 0;
int g_lastDac = -1;
int g_picBlitCalls = 0;
int g_lastPicBlitPage = -1;
SDL_IOStream *g_lastPicBlitHandle = nullptr;
int g_decodePicCalls = 0;
int g_showPicFileCalls = 0;
int g_setFadeStepsCalls = 0;
int g_lastFadeSteps = -1;

void require(bool condition, const char *message);

void resetStartRecorders() {
    g_drawCalls.clear();
    g_namedScriptedFiles.clear();
    g_nullOpenNames.clear();
    g_doFcbSearchResult = kWritableRoster;
    g_createCalls = 0;
    g_fileWriteCalls = 0;
    g_fileReadCalls = 0;
    g_fileCloseCalls = 0;
    g_openFileCalls = 0;
    g_openFileNullResponses = 0;
    g_scriptedFileBytes.clear();
    g_scriptedFileOffset = 0;
    g_activeScriptedFile = -1;
    g_clearCalls = 0;
    g_commitCalls = 0;
    g_flipCalls = 0;
    g_waitRetraceCalls = 0;
    g_getKeyCalls = 0;
    g_lastCreateAttr = -1;
    g_lastOpenMode = -1;
    g_lastClearX1 = -1;
    g_lastClearY1 = -1;
    g_lastClearX2 = -1;
    g_lastClearY2 = -1;
    g_centeredText.clear();
    g_lastOpenName.clear();
    std::memset(g_keyBufferScript, 0, sizeof(g_keyBufferScript));
    g_keyBufferScriptLen = 0;
    g_keyBufferScriptPos = 0;
    std::memset(g_getKeyScript, 0, sizeof(g_getKeyScript));
    g_getKeyScriptLen = 0;
    g_getKeyScriptPos = 0;
    std::memset(g_joystickScript, 0, sizeof(g_joystickScript));
    g_joystickScriptLen = 0;
    g_joystickScriptPos = 0;
    g_getKeyResult = kKeyboardSampleKey;
    g_checkKeyCalls = 0;
    g_joystickCalls = 0;
    g_cleanupCalls = 0;
    g_restoreCbreakCalls = 0;
    g_setPageNCalls = 0;
    g_lastSetPageN = -1;
    g_setColorCalls = 0;
    g_lastSetColor = -1;
    g_lineWrapperCalls = 0;
    g_nop23Calls = 0;
    g_pollJoystickCalls = 0;
    g_switchColorCalls = 0;
    g_lastSwitchX1 = -1;
    g_lastSwitchY1 = -1;
    g_lastSwitchX2 = -1;
    g_lastSwitchY2 = -1;
    g_lastSwitchFrom = -1;
    g_lastSwitchTo = -1;
    g_showSpriteCalls = 0;
    g_lastSpriteX = -1;
    g_lastSpriteY = -1;
    g_lastSpriteSrcX = -1;
    g_lastSpriteSrcY = -1;
    g_lastSpriteWidth = -1;
    g_lastSpriteHeight = -1;
    g_copyRectCalls = 0;
    g_blitToCurrentCalls = 0;
    g_timerYieldCalls = 0;
    g_setTimerCalls = 0;
    g_restoreTimerCalls = 0;
    g_timerCounterStepOnKeyCheck = 0;
    g_polledJoyX = kJoystickAxisCenter;
    g_polledJoyY = kJoystickAxisCenter;
    g_intDispatchCalls = 0;
    g_lastInterrupt = -1;
    g_setDacCalls = 0;
    g_lastDac = -1;
    g_picBlitCalls = 0;
    g_lastPicBlitPage = -1;
    g_lastPicBlitHandle = nullptr;
    g_decodePicCalls = 0;
    g_showPicFileCalls = 0;
    g_setFadeStepsCalls = 0;
    g_lastFadeSteps = -1;
    joyAxes[0] = kJoystickAxisCenter;
    joyAxes[1] = kJoystickAxisCenter;
    joyRepeatFlag = 0;
    timerCounter = 0;
}

void appendScriptedBytes(const void *ptr, size_t bytes) {
    const auto *src = static_cast<const uint8 *>(ptr);
    g_scriptedFileBytes.insert(g_scriptedFileBytes.end(), src, src + bytes);
}

void appendScriptedWord(uint16 value) {
    g_scriptedFileBytes.push_back(static_cast<uint8>(value & 0xff));
    g_scriptedFileBytes.push_back(static_cast<uint8>(value >> 8));
}

void appendNamedScriptedBytes(ScriptedFile &file, const void *src, size_t bytes) {
    const auto *data = static_cast<const uint8 *>(src);
    file.bytes.insert(file.bytes.end(), data, data + bytes);
}

void appendNamedScriptedWord(ScriptedFile &file, uint16 value) {
    file.bytes.push_back(static_cast<uint8>(value & 0xff));
    file.bytes.push_back(static_cast<uint8>(value >> 8));
}

void setKeyBufferScript(const int *values, int count) {
    require(count <= kInputScriptCapacity, "test key-buffer script capacity");
    std::memset(g_keyBufferScript, 0, sizeof(g_keyBufferScript));
    for (int idx = 0; idx < count; ++idx) g_keyBufferScript[idx] = values[idx];
    g_keyBufferScriptLen = count;
    g_keyBufferScriptPos = 0;
}

void setGetKeyScript(const int *values, int count) {
    require(count <= kInputScriptCapacity, "test get-key script capacity");
    std::memset(g_getKeyScript, 0, sizeof(g_getKeyScript));
    for (int idx = 0; idx < count; ++idx) g_getKeyScript[idx] = values[idx];
    g_getKeyScriptLen = count;
    g_getKeyScriptPos = 0;
}

void setJoystickScript(const int *values, int count) {
    require(count <= kInputScriptCapacity, "test joystick script capacity");
    std::memset(g_joystickScript, 0, sizeof(g_joystickScript));
    for (int idx = 0; idx < count; ++idx) g_joystickScript[idx] = values[idx];
    g_joystickScriptLen = count;
    g_joystickScriptPos = 0;
}

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

uint16 readLe16(const uint8 *bytes) {
    uint16 value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

std::string expectedGridRef(int wx, int wy, int theater) {
    char buf[kFormatGridBufSize] = {};
    int gridOffX = 0;
    int gridOffY = 0;
    switch (theater) {
    case kLibyaTheater:
        std::strcpy(buf, "TD00");
        gridOffX = kLibyaGridOffsetX;
        gridOffY = kLibyaGridOffsetY;
        break;
    case kDesertTheater:
        std::strcpy(buf, "JZ00");
        break;
    case kMediterraneanTheater:
        std::strcpy(buf, "XV00");
        break;
    case kEuropeTheater:
        std::strcpy(buf, "ES00");
        break;
    case kCentralEuropeTheater:
        std::strcpy(buf, "WX00");
        break;
    case kNorthCapeTheater:
        std::strcpy(buf, "CC00");
        gridOffX = kNorthCapeGridOffsetX;
        gridOffY = kNorthCapeGridOffsetY;
        break;
    case kPersianGulfTheater:
        std::strcpy(buf, "HZ00");
        break;
    default:
        return "";
    }

    wx = (((wx >> WORLD_COORD_SHIFT) * kWorldGridScale) >> kWorldGridDivisorShift) + gridOffX;
    while (wx > kGridDigitBase - 1) {
        wx -= kGridDigitBase;
        buf[0]++;
    }
    buf[2] += static_cast<char>(wx);

    wy = (((wy >> WORLD_COORD_SHIFT) * kWorldGridScale) >> kWorldGridDivisorShift) + gridOffY;
    while (wy > kGridDigitBase - 1) {
        wy -= kGridDigitBase;
        buf[1]--;
    }
    buf[3] += static_cast<char>(kGridYDigitInvert - wy);
    return buf;
}

} // namespace

int FAR CDECL gfx_setFont(uint16 ch, uint16 fontIdx) {
    (void)fontIdx;
    return ch == ' ' ? kSpaceGlyphWidth : kDefaultGlyphWidth;
}

void FAR CDECL gfx_drawString(int16 *page, const char *string) {
    g_drawCalls.push_back({page[kDrawPageXSlot], page[kDrawPageYSlot], string});
}

void drawStringAt(int16 *, const char *string, int x, int y) {
    g_drawCalls.push_back({x, y, string});
}

void doNothing2(const char *, int, int, int) {}

int stringWidth(int16 *, const char *string) {
    return static_cast<int>(std::strlen(string)) * kDefaultGlyphWidth;
}

void FAR CDECL misc_clearKeyFlags(void) {}

int doFcbSearch(void) { return g_doFcbSearchResult; }

SDL_IOStream *createFile(const char *, int attr) {
    ++g_createCalls;
    g_lastCreateAttr = attr;
    return reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kFakeFileHandle));
}

SDL_IOStream *openFile(const char *filename, int mode) {
    ++g_openFileCalls;
    g_lastOpenName = filename;
    g_lastOpenMode = mode;
    if (g_openFileNullResponses > 0) {
        --g_openFileNullResponses;
        return nullptr;
    }
    for (const std::string &nullName : g_nullOpenNames) {
        if (nullName == filename) return nullptr;
    }
    g_activeScriptedFile = -1;
    for (size_t idx = 0; idx < g_namedScriptedFiles.size(); ++idx) {
        if (g_namedScriptedFiles[idx].name == filename) {
            g_activeScriptedFile = static_cast<int>(idx);
            break;
        }
    }
    return reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kFakeOpenFileHandle));
}

size_t fileWrite(const void *ptr, size_t size, size_t count, SDL_IOStream *) {
    ++g_fileWriteCalls;
    if (g_fileWriteCalls == kExpectedOneCall) {
        require(ptr == &selectedPilotIdx && size == kHallfameSelectedWriteBytes &&
                    count == kHallfameSelectedWriteCount,
                "saveHallfame first writes the selected pilot index as the original word");
    } else {
        require(size == kHallfameRecordWriteSize && count == kHallfameRecordWriteCount,
                "saveHallfame writes each roster record with the original record size");
    }
    return count;
}

size_t fileRead(void *ptr, size_t size, size_t count, SDL_IOStream *) {
    ++g_fileReadCalls;
    if (g_activeScriptedFile >= 0) {
        ScriptedFile &file = g_namedScriptedFiles[static_cast<size_t>(g_activeScriptedFile)];
        const size_t bytes = size * count;
        require(file.offset + bytes <= file.bytes.size(),
                "named scripted fileRead must stay within the prepared stream");
        std::memcpy(ptr, file.bytes.data() + file.offset, bytes);
        file.offset += bytes;
        return count;
    }
    if (!g_scriptedFileBytes.empty()) {
        const size_t bytes = size * count;
        require(g_scriptedFileOffset + bytes <= g_scriptedFileBytes.size(),
                "scripted fileRead must stay within the prepared world stream");
        std::memcpy(ptr, g_scriptedFileBytes.data() + g_scriptedFileOffset, bytes);
        g_scriptedFileOffset += bytes;
        return count;
    }
    if (g_fileReadCalls == kExpectedOneCall) {
        require(size == kHallfameSelectedWriteBytes &&
                    count == kHallfameSelectedWriteCount,
                "loadHallfame first reads the selected pilot word");
        *static_cast<int *>(ptr) = kHallfameReadSelectedIdx;
    } else {
        require(size == kHallfameRecordWriteSize &&
                    count == kHallfameRecordWriteCount,
                "loadHallfame reads each original roster record size");
        Pilot *pilot = static_cast<Pilot *>(ptr);
        const int slotIdx = g_fileReadCalls - 2;
        std::memset(pilot, 0, sizeof(*pilot));
        std::snprintf(pilot->name, sizeof(pilot->name), "L%d", slotIdx);
        pilot->total_score = kHallfameReadBaseScore + slotIdx;
    }
    return count;
}

void fileClose(SDL_IOStream *) { ++g_fileCloseCalls; }
int mystrlen(const char *s) { return static_cast<int>(std::strlen(s)); }
char *mystrcat(char *dst, const char *src) { return std::strcat(dst, src); }
void nearmemset(void *dst, char value, int count) { std::memset(dst, value, count); }
void my_ltoa(int32 value, char *buf) {
    char tmp[32] = {};
    std::snprintf(tmp, sizeof(tmp), "%ld", static_cast<long>(value));
    const int len = static_cast<int>(std::strlen(tmp));
    int out = 0;
    for (int idx = 0; idx < len; ++idx) {
        if (idx > 0 && ((len - idx) % 3) == 0) buf[out++] = ',';
        buf[out++] = tmp[idx];
    }
    buf[out] = '\0';
}
void my_itoa(int value, char *buf) {
    my_ltoa(value, buf);
}

void clearRect(int16 *, int x1, int y1, int x2, int y2) {
    ++g_clearCalls;
    g_lastClearX1 = x1;
    g_lastClearY1 = y1;
    g_lastClearX2 = x2;
    g_lastClearY2 = y2;
}

void drawStringCentered(int16 *, const char *str, int, int, int) { g_centeredText.emplace_back(str); }
void FAR CDECL gfx_commitPage(void) { ++g_commitCalls; }
void FAR CDECL gfx_flipPage(void) { ++g_flipCalls; }
void FAR CDECL gfx_waitRetrace(void) { ++g_waitRetraceCalls; }
void FAR CDECL gfx_setDac(uint16 dac) {
    ++g_setDacCalls;
    g_lastDac = dac;
}
void intDispatch(int intNum, uint8 *, uint8 *) {
    ++g_intDispatchCalls;
    g_lastInterrupt = intNum;
}
int FAR CDECL misc_getKey(void) {
    ++g_getKeyCalls;
    if (g_getKeyScriptPos < g_getKeyScriptLen) return g_getKeyScript[g_getKeyScriptPos++];
    return g_getKeyResult;
}

int FAR CDECL misc_checkKeyBuf(void) {
    ++g_checkKeyCalls;
    timerCounter = static_cast<uint8>(timerCounter + g_timerCounterStepOnKeyCheck);
    if (g_keyBufferScriptPos < g_keyBufferScriptLen) return g_keyBufferScript[g_keyBufferScriptPos++];
    return kKeyBufferEmpty;
}

int FAR CDECL misc_readJoystick(int16 axis) {
    require(axis == kJoystickButtonAxis || axis == kJoystickAxisY,
            "start input reads the original joystick button axes");
    ++g_joystickCalls;
    if (g_joystickScriptPos < g_joystickScriptLen) return g_joystickScript[g_joystickScriptPos++];
    return kJoystickIdle;
}

void cleanup(void) { ++g_cleanupCalls; }
void restoreCbreakHandler(void) { ++g_restoreCbreakCalls; }
void FAR CDECL gfx_setPageN(uint16 pageNum) {
    ++g_setPageNCalls;
    g_lastSetPageN = pageNum;
}
void FAR CDECL gfx_setColor(int color) {
    ++g_setColorCalls;
    g_lastSetColor = color;
}
void drawLineWrapper(void) { ++g_lineWrapperCalls; }
void FAR CDECL gfx_nop23(void) { ++g_nop23Calls; }
void FAR CDECL gfx_switchColor(int16 *, int x1, int y1, int x2, int y2, int oldColor, int newColor) {
    ++g_switchColorCalls;
    g_lastSwitchX1 = x1;
    g_lastSwitchY1 = y1;
    g_lastSwitchX2 = x2;
    g_lastSwitchY2 = y2;
    g_lastSwitchFrom = oldColor;
    g_lastSwitchTo = newColor;
}
void FAR CDECL gfx_copyRect(int, uint16, uint16, int, uint16, uint16, int, int) { ++g_copyRectCalls; }
void FAR CDECL gfx_blitToCurrent(int16) { ++g_blitToCurrentCalls; }
void picBlit(SDL_IOStream *handle, int page) {
    ++g_picBlitCalls;
    g_lastPicBlitHandle = handle;
    g_lastPicBlitPage = page;
}
void decodePic(SDL_IOStream *, int) { ++g_decodePicCalls; }
void showPicFile(SDL_IOStream *, int) { ++g_showPicFileCalls; }
void FAR CDECL gfx_setFadeSteps(int steps) {
    ++g_setFadeStepsCalls;
    g_lastFadeSteps = steps;
}
void showSprite(int, int x, int y, int srcX, int srcY, int width, int height) {
    ++g_showSpriteCalls;
    g_lastSpriteX = x;
    g_lastSpriteY = y;
    g_lastSpriteSrcX = srcX;
    g_lastSpriteSrcY = srcY;
    g_lastSpriteWidth = width;
    g_lastSpriteHeight = height;
}
void timerYield(void) {
    ++g_timerYieldCalls;
    ++timerCounter3;
}
void setTimerIrqHandler(void) { ++g_setTimerCalls; }
void restoreTimerIrqHandler(void) { ++g_restoreTimerCalls; }
void far pollJoystick(void) {
    ++g_pollJoystickCalls;
    joyAxes[0] = static_cast<uint8>(g_polledJoyX);
    joyAxes[1] = static_cast<uint8>(g_polledJoyY);
}

int main() {
    struct Game game = {};
    struct GameComm comm = {};
    gameData = &game;
    commData = &comm;
    resetStartRecorders();

    for (auto [theater, wx, wy] : {std::tuple{kLibyaTheater, kSampleWorldX, kSampleWorldY},
                                   std::tuple{kDesertTheater, kDesertSampleWorldX, kDesertSampleWorldY},
                                   std::tuple{kMediterraneanTheater, kSampleWorldX, kSampleWorldY},
                                   std::tuple{kEuropeTheater, kSampleWorldX, kSampleWorldY},
                                   std::tuple{kCentralEuropeTheater, kSampleWorldX, kSampleWorldY},
                                   std::tuple{kNorthCapeTheater, kNorthCapeSampleWorldX, kNorthCapeSampleWorldY},
                                   std::tuple{kPersianGulfTheater, kSampleWorldX, kSampleWorldY}}) {
        game.theater = theater;
        require(std::strcmp(formatGridRef(wx, wy, theater), expectedGridRef(wx, wy, theater).c_str()) == 0,
                "formatGridRef preserves original theater offsets and digit wrapping");
    }

    game.theater = kInvalidTheater;
    require(std::strcmp(formatGridRef(kSampleWorldX, kSampleWorldY, kInvalidTheater), "") == 0,
            "formatGridRef returns empty string for unsupported theater");

    game.theater = kLibyaTheater;
    worldObjects[kSampleObjectSlot].x_coord = kSampleWorldX;
    worldObjects[kSampleObjectSlot].y_coord = kSampleWorldY;
    require(std::strcmp(getItemCoordStr(kSampleObjectSlot),
                        expectedGridRef(kSampleWorldX, kSampleWorldY, kLibyaTheater).c_str()) == 0,
            "getItemCoordStr formats the selected world object coordinates");

    worldObjects[kStgenObjectA].x_coord = kStgenObjectAX;
    worldObjects[kStgenObjectA].y_coord = kStgenObjectAY;
    worldObjects[kStgenObjectB].x_coord = kStgenObjectBX;
    worldObjects[kStgenObjectB].y_coord = kStgenObjectBY;
    require(itemDistance(kStgenObjectA, kStgenObjectB) == kStgenDistance,
            "itemDistance applies the original approximate Manhattan distance to world object coordinates");
    require(approxDistance(kStgenObjectBX - kStgenObjectAX,
                           kStgenObjectBY - kStgenObjectAY) == kStgenDistance &&
                approxDistance(kStgenApproxCapInput, kStgenApproxCapInput) == kStgenApproxCap,
            "approxDistance preserves original half-Manhattan distance and saturation cap");
    require(calcBearing(0, 1) == kStgenBearingNorth &&
                calcBearing(1, 0) == kStgenBearingEast &&
                static_cast<uint16>(calcBearing(0, -1)) == kStgenBearingSouth &&
                static_cast<uint16>(calcBearing(-1, 0)) == kStgenBearingWest,
            "calcBearing preserves original cardinal word-degree bearings");
    require(calcBearing(1, 1) == kStgenBearingNortheastApprox &&
                calcBearing(1, -1) == kStgenBearingSoutheastApprox &&
                static_cast<uint16>(calcBearing(-1, -1)) == kStgenBearingSouthwestApprox &&
                static_cast<uint16>(calcBearing(-1, 1)) == kStgenBearingNorthwestApprox,
            "calcBearing preserves original diagonal quadrant approximation");
    require(calcBearing(2, 1) == kStgenBearingShallowNortheastApprox &&
                calcBearing(2, -1) == kStgenBearingShallowSoutheastApprox &&
                static_cast<uint16>(calcBearing(-2, -1)) == kStgenBearingShallowSouthwestApprox &&
                static_cast<uint16>(calcBearing(-2, 1)) == kStgenBearingShallowNorthwestApprox,
            "calcBearing preserves original swapped-ratio quadrant approximation");
    require(clampValue(kStgenClampHi + 1, kStgenClampLo, kStgenClampHi) == kStgenClampHi &&
                clampValue(kStgenClampLo, kStgenClampLo, kStgenClampHi) == kStgenClampLo &&
                clampValue(kStgenClampLo - 1, kStgenClampLo, kStgenClampHi) == kStgenClampLo &&
                clampValue(kStgenClampWrapFloor, kStgenClampLo, kStgenClampHi) == kStgenClampHi,
            "clampValue preserves original wrap-floor high clamp");

    flightUnits[kStgenUnitSlot] = {};
    flightUnits[kStgenUnitSlot].planeType = kStgenPlaneType;
    worldObjects[kStgenObjectA].targetFlags = 0;
    positionUnit(kStgenUnitSlot, kStgenObjectA);
    require(flightUnits[kStgenUnitSlot].x == kStgenObjectAX + kStgenUnitXOffset &&
                flightUnits[kStgenUnitSlot].y == kStgenObjectAY + kStgenUnitYOffset &&
                flightUnits[kStgenUnitSlot].xPrecise == (static_cast<int32>(flightUnits[kStgenUnitSlot].x) << kStgenWorldShift) &&
                flightUnits[kStgenUnitSlot].yPrecise == (static_cast<int32>(flightUnits[kStgenUnitSlot].y) << kStgenWorldShift) &&
                flightUnits[kStgenUnitSlot].altitude == kStgenGroundAltitude &&
                flightUnits[kStgenUnitSlot].maxSpeed == planes[kStgenPlaneType].maxSpeed &&
                flightUnits[kStgenUnitSlot].heading == static_cast<int16>(kStgenUnitHeading) &&
                flightUnits[kStgenUnitSlot].flags == kStgenUnitFlags &&
                flightUnits[kStgenUnitSlot].waypointIdx == kStgenObjectA &&
                flightUnits[kStgenUnitSlot].fuel ==
                    (static_cast<long>(planes[kStgenPlaneType].range) << 0xd) /
                        planes[kStgenPlaneType].maxSpeed,
            "positionUnit preserves original offsets, world precision, speed, heading, flags, waypoint, and fuel");
    worldObjects[kStgenObjectA].targetFlags = 0x200;
    positionUnit(kStgenUnitSlot, kStgenObjectA);
    require(flightUnits[kStgenUnitSlot].altitude == kStgenCarrierAltitude,
            "positionUnit uses the original carrier/large-object altitude when target flag 0x200 is set");

    resetStartRecorders();
    setMoveDstComm7A("ignored.wld", "wb");
    require(moveDst == reinterpret_cast<uint8 *>(comm.worldBuf),
            "setMoveDstComm7A points the original export cursor at commData worldBuf");
    {
        int16 appendWords[kStgenAppendCount] = {0x1111, 0x2222, 0x3333};
        memAppend(appendWords, kStgenAppendItemSize, kStgenAppendCount,
                  reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kFakeFileHandle)));
        require(std::memcmp(comm.worldBuf, appendWords, kStgenAppendBytes) == 0 &&
                    moveDst == reinterpret_cast<uint8 *>(comm.worldBuf) + kStgenAppendBytes,
                "memAppend copies item-size times count bytes and advances the original export cursor");
    }
    doNothing(reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kFakeFileHandle)));

    resetStartRecorders();
    std::memset(comm.worldBuf, 0, sizeof(comm.worldBuf));
    std::memset(worldObjects, 0, sizeof(struct WorldObject) * kStgenExportReadItemSize);
    std::memset(flightUnits, 0, sizeof(struct FlightUnit) * kStgenExportFlightUnitCount);
    std::memset(wldReadBuf1, 0, kStgenExportCountWordBytes);
    std::memset(wldReadBuf7, 0, kStgenExportSmallTableBytes);
    std::memset(wldReadBuf8, 0, kStgenExportSmallTableBytes);
    std::memset(wldReadBuf11, 0, WORLD_BUFSZ);
    std::memset(terrainGrid, 0, kStgenExportTerrainBytes);
    wldReadBuf1[0] = kStgenExportSignatureLow;
    wldReadBuf1[1] = kStgenExportSignatureHigh;
    readItemSize = kStgenExportReadItemSize;
    groundUnitCount = kStgenExportGroundUnitCount;
    worldObjectCount = kStgenExportWorldObjectCount;
    flightUnitCount = kStgenExportFlightUnitCount;
    worldObjects[0].x_coord = kStgenExportObjectX;
    worldObjects[0].y_coord = kStgenExportObjectY;
    missionDistAccum = kStgenExportMissionDist;
    escortMissionFlag = kStgenExportEscortFlag;
    missionMidX = kStgenExportMidX;
    missionMidY = kStgenExportMidY;
    missionTargetX = kStgenExportTargetX;
    missionTargetY = kStgenExportTargetY;
    missionTarget2X = kStgenExportTarget2X;
    missionTarget2Y = kStgenExportTarget2Y;
    missionBase2X = kStgenExportBase2X;
    missionBase2Y = kStgenExportBase2Y;
    exportWorldToComm("ignored.wld");
    require(readLe16(comm.worldBuf) == kStgenExportSignatureWord &&
                readLe16(comm.worldBuf + 2) == kStgenExportReadItemSize &&
                readLe16(comm.worldBuf + 4) == kStgenExportGroundUnitCount &&
                readLe16(comm.worldBuf + 6) == kStgenExportWorldObjectCount,
            "exportWorldToComm writes the original world header words");
    require(readLe16(comm.worldBuf + kStgenExportObjectsOffset + kStgenWorldObjectXOffset) == kStgenExportObjectX &&
                readLe16(comm.worldBuf + kStgenExportObjectsOffset + kStgenWorldObjectYOffset) == kStgenExportObjectY &&
                readLe16(comm.worldBuf + kStgenExportFlightCountOffset) == kStgenExportFlightUnitCount,
            "exportWorldToComm writes world objects followed by the original flight-unit count");
    require(readLe16(comm.worldBuf + kStgenExportMissionFieldsOffset) == kStgenExportMissionDist &&
                readLe16(comm.worldBuf + kStgenExportMissionFieldsOffset + 2) == kStgenExportEscortFlag &&
                readLe16(comm.worldBuf + kStgenExportMissionFieldsOffset + 4) == kStgenExportMidX &&
                readLe16(comm.worldBuf + kStgenExportMissionFieldsOffset + 6) == kStgenExportMidY &&
                readLe16(comm.worldBuf + kStgenExportMissionFieldsOffset + 8) == kStgenExportTargetX &&
                readLe16(comm.worldBuf + kStgenExportMissionFieldsOffset + 10) == kStgenExportTargetY &&
                readLe16(comm.worldBuf + kStgenExportMissionFieldsOffset + 12) == kStgenExportTarget2X &&
                readLe16(comm.worldBuf + kStgenExportMissionFieldsOffset + 14) == kStgenExportTarget2Y &&
                readLe16(comm.worldBuf + kStgenExportMissionFieldsOffset + 16) == kStgenExportBase2X &&
                readLe16(comm.worldBuf + kStgenExportMissionFieldsOffset + 18) == kStgenExportBase2Y &&
                moveDst == reinterpret_cast<uint8 *>(comm.worldBuf) + kStgenExportTotalBytes,
            "exportWorldToComm preserves the original trailing mission-field order and total byte count");

    resetStartRecorders();
    {
        struct WorldObject scriptedObjects[kParseWorldReadItemSize] = {};
        struct FlightUnit scriptedUnits[kParseWorldFlightUnitCount] = {};
        uint8 buf7[BUF7SIZE] = {};
        uint8 buf8[BUF7SIZE] = {};
        uint8 objectTypes[BUF7SIZE] = {};
        uint8 terrain[BUF10SIZE] = {};
        char names[WORLD_BUFSZ] = {};

        scriptedObjects[0].unitRef = kParseWorldObject0UnitRef;
        scriptedObjects[0].x_coord = kParseWorldObject0X;
        scriptedObjects[0].y_coord = kParseWorldObject0Y;
        scriptedObjects[0].unitType = kParseWorldObject0Type;
        scriptedObjects[0].targetFlags = kParseWorldObject0Flags;
        scriptedObjects[0].objectIdx = kParseWorldObject0ObjectIdx;
        scriptedUnits[0].waypointIdx = kParseWorldFlightWaypoint;
        scriptedUnits[0].x = kParseWorldFlightX;
        scriptedUnits[0].y = kParseWorldFlightY;
        scriptedUnits[0].planeType = kParseWorldFlightPlaneType;
        std::memset(buf7, kParseWorldBuf7Byte, sizeof(buf7));
        std::memset(buf8, kParseWorldBuf8Byte, sizeof(buf8));
        std::memset(objectTypes, kParseWorldObjectTypeByte, sizeof(objectTypes));
        std::memset(terrain, kParseWorldTerrainByte, sizeof(terrain));
        std::memcpy(names, "Alpha\0Bravo\0Charlie", sizeof("Alpha\0Bravo\0Charlie"));

        g_scriptedFileBytes.push_back(kParseWorldSignatureLow);
        g_scriptedFileBytes.push_back(kParseWorldSignatureHigh);
        appendScriptedWord(kParseWorldReadItemSize);
        appendScriptedWord(kParseWorldGroundUnitCount);
        appendScriptedWord(kParseWorldWorldObjectCount);
        appendScriptedBytes(scriptedObjects, sizeof(scriptedObjects));
        appendScriptedWord(kParseWorldFlightUnitCount);
        appendScriptedBytes(scriptedUnits, sizeof(scriptedUnits));
        appendScriptedBytes(buf7, sizeof(buf7));
        appendScriptedBytes(buf8, sizeof(buf8));
        appendScriptedBytes(objectTypes, sizeof(objectTypes));
        appendScriptedBytes(terrain, sizeof(terrain));
        appendScriptedBytes(names, sizeof(names));

        parseWorld("SCRIPT.WLD");
        require(g_openFileCalls == kExpectedOneCall &&
                    g_lastOpenMode == kF15SprOpenMode &&
                    g_lastOpenName == "SCRIPT.WLD" &&
                    g_fileCloseCalls == kExpectedOneCall &&
                    g_scriptedFileOffset == g_scriptedFileBytes.size(),
                "parseWorld reads the original packed world stream and closes the file");
        require(wldReadBuf1[0] == kParseWorldSignatureLow &&
                    wldReadBuf1[1] == kParseWorldSignatureHigh &&
                    readItemSize == kParseWorldReadItemSize &&
                    groundUnitCount == kParseWorldGroundUnitCount &&
                    worldObjectCount == kParseWorldWorldObjectCount &&
                    flightUnitCount == kParseWorldFlightUnitCount,
                "parseWorld loads the original world header counts");
        require(worldObjects[0].unitRef == kParseWorldObject0UnitRef &&
                    worldObjects[0].x_coord == kParseWorldObject0X &&
                    worldObjects[0].y_coord == kParseWorldObject0Y &&
                    worldObjects[0].unitType == kParseWorldObject0Type &&
                    worldObjects[0].targetFlags == kParseWorldObject0Flags &&
                    worldObjects[0].objectIdx == kParseWorldObject0ObjectIdx &&
                    flightUnits[0].waypointIdx == kParseWorldFlightWaypoint &&
                    flightUnits[0].x == kParseWorldFlightX &&
                    flightUnits[0].y == kParseWorldFlightY &&
                    flightUnits[0].planeType == kParseWorldFlightPlaneType,
                "parseWorld loads original world object and flight-unit records");
	        require(wldReadBuf7[0] == kParseWorldBuf7Byte &&
	                    wldReadBuf8[0] == kParseWorldBuf8Byte &&
	                    static_cast<unsigned char>(objectTypeTable[0]) == kParseWorldObjectTypeByte &&
	                    static_cast<unsigned char>(terrainGrid[0]) == kParseWorldTerrainByte &&
	                    std::strcmp(wldOffsets[0], "Alpha") == 0 &&
	                    std::strcmp(wldOffsets[1], "Bravo") == 0 &&
	                    std::strcmp(wldOffsets[2], "Charlie") == 0,
	                "parseWorld rebuilds original world lookup tables and packed-name offsets");
	    }

	    resetStartRecorders();
	    std::srand(1);
	    auto configureRunGeneratorFixture = [&](int pick) {
	        std::memset(worldObjects, 0, sizeof(struct WorldObject) * kRunGeneratorWorldObjectCapacity);
	        std::memset(flightUnits, 0, sizeof(struct FlightUnit) * kRunGeneratorFlightUnitCapacity);
	        std::memset(targets, 0, sizeof(struct Target) * 2);
	        std::memset(terrainGrid, 0, sizeof(terrainGrid));
	        std::memset(objectTypeTable, 0, kObjectTypeTableBytes);
	        std::memset(terrainTileCounts, 0, sizeof(struct TerrainCountTable) * kTerrainCountTableCount);
	        std::memset(gridBuf1, 0, kGridTopLevelBytes);
	        std::memset(gridBuf2, 0, kGridLevel2Bytes);
	        std::memset(gridBuf3, 0, kGridSubLevelBytes);
	        std::memset(gridBuf4, 0, kGridSubLevelBytes);
	        std::memset(gridBuf5, 0, kGridSubLevelBytes);
	        game.theater = kPersianGulfTheater;
	        game.difficulty = kMissionSelectDifficulty;
	        game.isCampaignMission = 0;
	        missionPick = pick;
	        readItemSize = kRunGeneratorReadItemSize;
	        worldObjectCount = kRunGeneratorWorldObjectCount;
	        groundUnitCount = kRunGeneratorGroundUnitCount;
	        flightUnitCount = kRunGeneratorFlightUnitCount;
	        wldOffsets[kRunGeneratorPrimaryTargetSlot] = const_cast<char *>("Primary");
	        wldOffsets[kRunGeneratorSecondaryTargetSlot] = const_cast<char *>("Secondary");
	        objectTypeTable[kRunGeneratorTerrainObjectType] = kRunGeneratorTerrainObjectType;
	        terrainTilePtrs[kGridLevel1].entries[kRunGeneratorTerrainCell] = terrainTileBlock;
	        terrainTileCounts[kGridLevel1].entries[kRunGeneratorTerrainCell] = kRunGeneratorTerrainTileCount;
	        terrainTileBlock[0].buf3 = 0;
	        terrainTileBlock[0].buf4 = 0;
	        terrainTileBlock[0].buf5 = 0;
	        terrainTileBlock[0].idx = kRunGeneratorTerrainObjectType;
	        worldObjects[kRunGeneratorPrimaryBaseSlot].x_coord = kRunGeneratorPrimaryBaseX;
	        worldObjects[kRunGeneratorPrimaryBaseSlot].y_coord = kRunGeneratorPrimaryBaseY;
	        worldObjects[kRunGeneratorPrimaryBaseSlot].unitRef = kRunGeneratorBaseUnitRef;
	        worldObjects[kRunGeneratorPrimaryBaseSlot].targetFlags = kRunGeneratorBaseFlags;
	        worldObjects[kRunGeneratorSecondaryBaseSlot].x_coord = kRunGeneratorSecondaryBaseX;
	        worldObjects[kRunGeneratorSecondaryBaseSlot].y_coord = kRunGeneratorSecondaryBaseY;
	        worldObjects[kRunGeneratorSecondaryBaseSlot].unitRef = kRunGeneratorBaseUnitRef;
	        worldObjects[kRunGeneratorSecondaryBaseSlot].targetFlags = kRunGeneratorBaseFlags;
	    };
	    auto configureRunGeneratorRichFixture = [&](int pick) {
	        configureRunGeneratorFixture(pick);
	        readItemSize = kRunGeneratorRichReadItemSize;
	        worldObjectCount = kRunGeneratorRichWorldObjectCount;
	        groundUnitCount = kRunGeneratorRichGroundUnitCount;
	        flightUnitCount = kRunGeneratorRichFlightUnitCount;
	        for (int idx = FIRST_REAL_ITEM; idx < kRunGeneratorRichWorldObjectCount; ++idx) {
	            worldObjects[idx].x_coord = kRunGeneratorPrimaryBaseX + idx * kWorldGridScale;
	            worldObjects[idx].y_coord = kRunGeneratorPrimaryBaseY + idx * kWorldGridScale;
	            worldObjects[idx].targetFlags = kRunGeneratorPatrolCandidateFlags;
	        }
	        worldObjects[0].unitType = kRunGeneratorGroundRemapType;
	        worldObjects[1].unitType = kRunGeneratorGroundRemapType;
	        worldObjects[1].targetFlags = 8; /* Original optional-removal flag for ground units. */
	        worldObjects[2].unitType = kRunGeneratorSpecialUnitType;
	        worldObjects[kRunGeneratorRichWorldObjectCount].x_coord = kRunGeneratorPrimaryBaseX;
	        worldObjects[kRunGeneratorRichWorldObjectCount].y_coord = kRunGeneratorPrimaryBaseY;
	        worldObjects[kRunGeneratorRichWorldObjectCount].targetFlags = kRunGeneratorBaseFlags;
	        worldObjects[kRunGeneratorRichWorldObjectCount + 1].x_coord = kRunGeneratorSecondaryBaseX;
	        worldObjects[kRunGeneratorRichWorldObjectCount + 1].y_coord = kRunGeneratorSecondaryBaseY;
	        worldObjects[kRunGeneratorRichWorldObjectCount + 1].targetFlags = kRunGeneratorBaseFlags;
	    };
	    configureRunGeneratorFixture(kRunGeneratorMissionPick);
	    runGenerator();
	    require(targets[0].targetIdx == kRunGeneratorPrimaryTargetSlot &&
	                targets[1].targetIdx == kRunGeneratorSecondaryTargetSlot &&
	                worldObjects[targets[0].targetIdx].objectIdx == kRunGeneratorTerrainObjectIdx &&
	                worldObjects[targets[1].targetIdx].objectIdx == kRunGeneratorTerrainObjectIdx,
	            "runGenerator places historical targets through the original terrain lookup path");
	    require(targets[0].missionType != 0 &&
	                targets[1].missionType != 0 &&
	                targets[0].baseIdx >= kRunGeneratorPrimaryBaseSlot &&
	                targets[1].baseIdx >= kRunGeneratorPrimaryBaseSlot,
	            "runGenerator assigns original mission table entries and valid base slots");
	    require(comm.weaponType[0] == kRunGeneratorDefaultWeapon0 &&
	                comm.weaponType[1] == kRunGeneratorDefaultWeapon1 &&
	                comm.weaponType[2] == kRunGeneratorHistoricalWeapon2 &&
	                comm.weaponCount[0] == weaponLoadouts[comm.weaponType[0]].qty &&
	                comm.weaponCount[1] == weaponLoadouts[comm.weaponType[1]].qty &&
	                comm.weaponCount[2] == weaponLoadouts[comm.weaponType[2]].qty,
	            "runGenerator fills the original Desert Storm historical weapon loadout");
	    require(baseXPrecise == (static_cast<uint32>(worldObjects[targets[0].baseIdx].x_coord) << WORLD_COORD_SHIFT) &&
	                missionTargetX == worldObjects[targets[0].targetIdx].x_coord &&
	                missionTargetY == worldObjects[targets[0].targetIdx].y_coord &&
	                missionBase2X == worldObjects[targets[1].baseIdx].x_coord &&
	                missionBase2Y == worldObjects[targets[1].baseIdx].y_coord,
	            "runGenerator exports original base and target mission coordinates");

	    resetStartRecorders();
	    std::srand(1);
	    configureRunGeneratorFixture(kRunGeneratorAltCoordsMissionPick);
	    runGenerator();
	    require(targets[0].targetIdx == kRunGeneratorPrimaryTargetSlot &&
	                targets[1].targetIdx == kRunGeneratorSecondaryTargetSlot &&
	                comm.weaponType[2] == missionPickType[kRunGeneratorAltCoordsMissionPick],
	            "runGenerator preserves the original historical mission-pick-2 alternate secondary target path");

	    resetStartRecorders();
	    std::srand(1);
	    configureRunGeneratorFixture(kRunGeneratorOffsetSecondTargetMissionPick);
	    runGenerator();
	    require(targets[0].targetIdx == kRunGeneratorPrimaryTargetSlot &&
	                targets[1].targetIdx == kRunGeneratorSecondaryTargetSlot &&
	                comm.weaponType[2] == missionPickType[kRunGeneratorOffsetSecondTargetMissionPick],
	            "runGenerator preserves the original historical mission-pick-7 offset secondary target path");

	    resetStartRecorders();
	    std::srand(1);
	    configureRunGeneratorFixture(kRunGeneratorRandomMissionPick);
	    runGenerator();
	    require(targets[0].targetIdx == kRunGeneratorPrimaryTargetSlot &&
	                targets[1].targetIdx == kRunGeneratorSecondaryTargetSlot &&
	                worldObjects[targets[0].targetIdx].objectIdx == kRunGeneratorTerrainObjectIdx &&
	                worldObjects[targets[1].targetIdx].objectIdx == kRunGeneratorTerrainObjectIdx,
	            "runGenerator places random primary and secondary targets through the original terrain retry path");
	    require(comm.weaponType[0] == kRunGeneratorDefaultWeapon0 &&
	                comm.weaponType[1] == kRunGeneratorDefaultWeapon1 &&
	                comm.weaponType[2] == kRunGeneratorDefaultWeapon2,
	            "runGenerator preserves original non-historical Desert Storm default weapon loadout");

	    resetStartRecorders();
	    std::srand(1);
	    configureRunGeneratorRichFixture(kRunGeneratorRandomMissionPick);
	    flightUnits[0].planeType = kParseWorldFlightPlaneType;
	    flightUnits[0].flags = kRunGeneratorFlightPatrolFlags | kRunGeneratorFlightWideRangeFlag;
	    flightUnits[1].planeType = kParseWorldFlightPlaneType;
	    terrainGrid[0] = kRunGeneratorTerrainObjectFlag;
	    runGenerator();
	    require((flightUnits[0].flags & kStgenUnitFlags) == kStgenUnitFlags &&
	                flightUnits[0].waypointIdx >= FIRST_REAL_ITEM &&
	                flightUnits[1].waypointIdx == 0,
	            "runGenerator preserves original patrol placement for pre-flagged flight units");
	    require(worldObjects[flightUnits[1].waypointIdx].patrolCount != 0 ||
	                worldObjects[FIRST_REAL_ITEM].patrolCount != 0,
	            "runGenerator assigns original non-player patrol occupants after mission generation");
	    require(worldObjects[0].unitType != kRunGeneratorGroundRemapType ||
	                worldObjects[1].unitType != kRunGeneratorGroundRemapType ||
	                terrainGrid[0] == 0,
	            "runGenerator exercises original ground-unit remap/removal and terrain cleanup branches");

	    resetStartRecorders();
	    std::srand(1);
	    configureRunGeneratorFixture(kRunGeneratorMissionPick);
	    game.theater = kRunGeneratorNonDsTheater;
	    readItemSize = kRunGeneratorRichReadItemSize;
	    worldObjectCount = FIRST_REAL_ITEM + kRunGeneratorHistoricalTargetCount;
	    for (int idx = 0; idx < kRunGeneratorHistoricalTargetCount; ++idx) {
	        const int objectSlot = FIRST_REAL_ITEM + idx;
	        worldObjects[objectSlot].x_coord = targetCoordsX6[idx];
	        worldObjects[objectSlot].y_coord = targetCoordsY6[idx];
	        worldObjects[objectSlot].objectIdx =
	            kRunGeneratorTerrainObjectType + idx * kRunGeneratorObjectIdxStride;
	        worldObjects[objectSlot].targetFlags = kRunGeneratorPatrolCandidateFlags;
	        wldOffsets[objectSlot] = const_cast<char *>("Historical");
	    }
	    worldObjects[worldObjectCount].x_coord = kRunGeneratorFarBaseCoord;
	    worldObjects[worldObjectCount].y_coord = kRunGeneratorFarBaseCoord;
	    worldObjects[worldObjectCount].unitRef = kRunGeneratorBaseUnitRef;
	    worldObjects[worldObjectCount].targetFlags = kRunGeneratorBaseFlags;
	    runGenerator();
	    require(game.theater == kRunGeneratorNonDsTheater &&
	                targets[0].targetIdx >= FIRST_REAL_ITEM &&
	                targets[1].targetIdx >= FIRST_REAL_ITEM &&
	                worldObjects[targets[0].targetIdx].objectIdx !=
	                    worldObjects[targets[1].targetIdx].objectIdx,
	            "runGenerator preserves original non-Desert Storm target retry and distance acceptance");

	    resetStartRecorders();
	    std::srand(1);
	    configureRunGeneratorFixture(kRunGeneratorRandomMissionPick);
	    objectTypeTable[kRunGeneratorTerrainObjectType] = kRunGeneratorPositiveObjectFlagTension;
	    runGenerator();
	    require((targets[0].missionCode & kRunGeneratorObjectFlagMissionCodeMask) != 0 &&
	                (targets[1].missionCode & kRunGeneratorObjectFlagMissionCodeMask) != 0,
	            "runGenerator preserves original positive object-flag mission-code encoding");

	    {
	        bool foundSnapMission = false;
	        bool foundLandingSecondary = false;
	        for (int seed = 1; seed <= kRunGeneratorSeedSearchLimit &&
	                           (!foundSnapMission || !foundLandingSecondary);
	             ++seed) {
	            resetStartRecorders();
	            std::srand(seed);
	            configureRunGeneratorFixture(kRunGeneratorRandomMissionPick);
	            objectTypeTable[kRunGeneratorTerrainObjectType] = kRunGeneratorSnapMissionTension;
	            runGenerator();
	            foundSnapMission = foundSnapMission ||
	                               ((targets[0].missionCode & kRunGeneratorSnapMissionCodeBit) != 0 &&
	                                (missionTargetX & kRunGeneratorGridCellMask) ==
	                                    kRunGeneratorGridCellCenter &&
	                                (missionTargetY & kRunGeneratorGridCellMask) ==
	                                    kRunGeneratorGridCellCenter);
	            foundLandingSecondary = foundLandingSecondary ||
	                                    (targets[1].missionType == kRunGeneratorLandingMissionType &&
	                                     nightMissionFlag == 1);
	        }
	        require(foundSnapMission,
	                "runGenerator preserves original grid-centered coordinates for mission-code bit 0x10");
	        require(foundLandingSecondary,
	                "runGenerator preserves original night flag for landing-type secondary missions");
	    }

	    {
	        bool foundDowngrade = false;
	        for (int seed = 1; seed <= kRunGeneratorSeedSearchLimit && !foundDowngrade; ++seed) {
	            resetStartRecorders();
	            std::srand(seed);
	            configureRunGeneratorRichFixture(kRunGeneratorRandomMissionPick);
	            game.difficulty = 0;
	            runGenerator();
	            foundDowngrade =
	                worldObjects[0].unitType ==
	                unitTypeRemapTable[kRunGeneratorGroundRemapType].downgrade;
	        }
	        require(foundDowngrade,
	                "runGenerator preserves original low-difficulty ground-unit downgrade remap");
	    }

	    {
	        bool foundEscortPlaneOverride = false;
	        for (int seed = 1; seed <= kRunGeneratorSeedSearchLimit && !foundEscortPlaneOverride;
	             ++seed) {
	            resetStartRecorders();
	            std::srand(seed);
	            configureRunGeneratorFixture(kRunGeneratorRandomMissionPick);
	            objectTypeTable[kRunGeneratorTerrainObjectType] = kRunGeneratorEscortMissionTension;
	            runGenerator();
	            foundEscortPlaneOverride = flightUnits[0].planeType != 0;
	        }
	        require(foundEscortPlaneOverride,
	                "runGenerator preserves original negative object-flag plane override");
	    }

	    resetStartRecorders();
	    std::srand(1);
	    std::memset(&game, 0, sizeof(game));
	    std::memset(&comm, 0, sizeof(comm));
	    game.theater = kPersianGulfTheater;
	    game.difficulty = kMissionSelectDifficulty;
	    game.isCampaignMission = 0;
	    missionPick = kRunGeneratorMissionPick;
	    {
	        struct WorldObject scriptedObjects[kRunGeneratorReadItemSize] = {};
	        struct FlightUnit scriptedUnits[kRunGeneratorFlightUnitCount] = {};
	        uint8 buf7[BUF7SIZE] = {};
	        uint8 buf8[BUF7SIZE] = {};
	        uint8 objectTypes[BUF7SIZE] = {};
	        uint8 terrain[BUF10SIZE] = {};
	        char names[WORLD_BUFSZ] = {};
	        uint16 terrainCounts[5] = {};
	        uint8 grid1[16] = {};
	        uint8 grid2[0x100] = {};
	        uint8 gridN[0x200] = {};

	        scriptedObjects[kRunGeneratorPrimaryBaseSlot].x_coord = kRunGeneratorPrimaryBaseX;
	        scriptedObjects[kRunGeneratorPrimaryBaseSlot].y_coord = kRunGeneratorPrimaryBaseY;
	        scriptedObjects[kRunGeneratorPrimaryBaseSlot].targetFlags = kRunGeneratorBaseFlags;
	        scriptedObjects[kRunGeneratorSecondaryBaseSlot].x_coord = kRunGeneratorSecondaryBaseX;
	        scriptedObjects[kRunGeneratorSecondaryBaseSlot].y_coord = kRunGeneratorSecondaryBaseY;
	        scriptedObjects[kRunGeneratorSecondaryBaseSlot].targetFlags = kRunGeneratorBaseFlags;
	        objectTypes[kRunGeneratorTerrainObjectType] = kRunGeneratorTerrainObjectType;
	        std::memcpy(names, "Root\0Primary\0Secondary", sizeof("Root\0Primary\0Secondary"));
	        terrainCounts[kGridLevel1] = kRunGeneratorTerrainTileCount;

	        ScriptedFile world{"jp.wld"};
	        world.bytes.push_back(kParseWorldSignatureLow);
	        world.bytes.push_back(kParseWorldSignatureHigh);
	        appendNamedScriptedWord(world, kRunGeneratorReadItemSize);
	        appendNamedScriptedWord(world, kRunGeneratorGroundUnitCount);
	        appendNamedScriptedWord(world, kRunGeneratorWorldObjectCount);
	        appendNamedScriptedBytes(world, scriptedObjects, sizeof(scriptedObjects));
	        appendNamedScriptedWord(world, kRunGeneratorFlightUnitCount);
	        appendNamedScriptedBytes(world, scriptedUnits, sizeof(scriptedUnits));
	        appendNamedScriptedBytes(world, buf7, sizeof(buf7));
	        appendNamedScriptedBytes(world, buf8, sizeof(buf8));
	        appendNamedScriptedBytes(world, objectTypes, sizeof(objectTypes));
	        appendNamedScriptedBytes(world, terrain, sizeof(terrain));
	        appendNamedScriptedBytes(world, names, sizeof(names));
	        g_namedScriptedFiles.push_back(std::move(world));

	        ScriptedFile grid{"jp.3dG"};
	        appendNamedScriptedWord(grid, kGridMagic);
	        appendNamedScriptedBytes(grid, grid1, sizeof(grid1));
	        appendNamedScriptedBytes(grid, grid2, sizeof(grid2));
	        appendNamedScriptedBytes(grid, gridN, sizeof(gridN));
	        appendNamedScriptedBytes(grid, gridN, sizeof(gridN));
	        appendNamedScriptedBytes(grid, gridN, sizeof(gridN));
	        g_namedScriptedFiles.push_back(std::move(grid));

	        ScriptedFile terrainFile{"jp.3dT"};
	        appendNamedScriptedWord(terrainFile, kTerrainMagic);
	        appendNamedScriptedBytes(terrainFile, terrainCounts, sizeof(terrainCounts));
	        appendNamedScriptedWord(terrainFile, kRunGeneratorTerrainTileCount);
	        appendNamedScriptedWord(terrainFile, 0);
	        appendNamedScriptedWord(terrainFile, 0);
	        appendNamedScriptedWord(terrainFile, 0);
	        appendNamedScriptedWord(terrainFile, kRunGeneratorTerrainObjectType);
	        g_namedScriptedFiles.push_back(std::move(terrainFile));
	    }
	    missionGenerate();
	    require(g_openFileCalls == kMissionGenerateExpectedOpenCount &&
	                g_namedScriptedFiles[0].offset == g_namedScriptedFiles[0].bytes.size() &&
	                g_namedScriptedFiles[1].offset == g_namedScriptedFiles[1].bytes.size() &&
	                g_namedScriptedFiles[2].offset == g_namedScriptedFiles[2].bytes.size(),
	            "missionGenerate reads the original world, grid, and terrain files");
	    require(difficultySaved == kMissionSelectDifficulty &&
	                theaterSaved == (kPersianGulfTheater & kMissionGenerateSavedTheaterMask) &&
	                flag4Saved == 0 &&
	                terrainDirtyFlag == 0,
	            "missionGenerate saves original mission context and clears terrain dirty state");
	    require(targets[0].targetIdx == kRunGeneratorPrimaryTargetSlot &&
	                targets[1].targetIdx == kRunGeneratorSecondaryTargetSlot &&
	                comm.weaponType[2] == kRunGeneratorHistoricalWeapon2,
	            "missionGenerate drives parseWorld, parseGridTerrain, and runGenerator as the original wrapper");

	    wldOffsets[kStgenTargetObjectName] = const_cast<char *>("Depot");
    wldOffsets[kStgenTargetUnitName] = const_cast<char *>("Tripoli");
    worldObjects[kStgenTargetLabelIdx].objectIdx = kStgenTargetObjectName;
    worldObjects[kStgenTargetLabelIdx].unitRef = kStgenTargetUnitName;
    missionStrTrunc = 0;
    missionStrTruncEnd[0] = 'X';
    buildTargetLabel(kStgenTargetLabelIdx);
    require(std::strcmp(todayMissStrBuf, "Depot at Tripoli") == 0 &&
                missionStrTrunc == 0 &&
                missionStrTruncEnd[0] == 'X',
            "buildTargetLabel joins original object and unit names when both are present");
    wldOffsets[kStgenLongObjectName] = const_cast<char *>("Very Long Strategic Radar Installation");
    worldObjects[kStgenLongLabelIdx].objectIdx = kStgenLongObjectName;
    worldObjects[kStgenLongLabelIdx].unitRef = 0;
    wldOffsets[0] = const_cast<char *>("");
    buildTargetLabel(kStgenLongLabelIdx);
    require(missionStrTrunc == 46 &&
                missionStrTruncEnd[0] == 0,
            "buildTargetLabel marks labels longer than the original truncation threshold");

    int16 page[kDrawPageWordCount] = {};
    page[kDrawPageFontSlot] = kWrappedFont;
    char wrapped[] = "ALPHA BETA-GAMMA\rDELTA";
    drawWrappedText(page, wrapped, kWrappedMaxWidth, kWrappedX, kWrappedStartY, kWrappedLineHeight);
    require(g_drawCalls.size() == kWrappedLineCount, "drawWrappedText emits the original number of wrapped lines");
    require(g_drawCalls[0].x == kWrappedX && g_drawCalls[0].y == kWrappedStartY &&
                g_drawCalls[0].text == "ALPHA",
            "drawWrappedText draws first wrapped word at requested position");
    require(g_drawCalls[1].y == kWrappedStartY + kWrappedLineHeight &&
                g_drawCalls[1].text == "BETA-",
            "drawWrappedText keeps a trailing hyphen on the current line");
    require(g_drawCalls[2].y == kWrappedStartY + kWrappedLineHeight * kGridLevel2 &&
                g_drawCalls[2].text == "GAMMA",
            "drawWrappedText wraps remaining hyphenated text");
    require(g_drawCalls[3].y == kWrappedStartY + kWrappedLineHeight * kGridLevel3 + kCarriageReturnExtraY &&
                g_drawCalls[3].text == "DELTA",
            "drawWrappedText applies the original carriage-return extra spacing");

    std::memset(gridBuf1, 0, kGridTopLevelBytes);
    std::memset(gridBuf2, 0, kGridLevel2Bytes);
    std::memset(gridBuf3, 0, kGridSubLevelBytes);
    std::memset(gridBuf4, 0, kGridSubLevelBytes);
    std::memset(gridBuf5, 0, kGridSubLevelBytes);
    gridBuf1[2 + (1 << 2)] = kTerrainParentCell;
    gridBuf2[3 + (4 << 4)] = kTerrainParentCell;
    gridBuf2[1 + (2 << 4)] = kTerrainParentCell;
    gridBuf2[0] = kTerrainParentCell;
    gridBuf3[1 + ((2 << 2) + (kTerrainParentCell << 4))] = kTerrainParentCell;
    gridBuf3[1 + ((1 << 2) + (kTerrainParentCell << 4))] = kTerrainLeafCell;
    gridBuf3[kTerrainParentCell << 4] = kTerrainParentCell;
    gridBuf4[1 + ((1 << 2) + (kTerrainParentCell << 4))] = kTerrainLeafCell;
    gridBuf4[1 + ((1 << 2) + (kTerrainLeafCell << 4))] = kTerrainLeafCell;
    // These indices exercise the original packed quadtree addressing formula.
    gridBuf5[2 + ((3 << 2) + (kTerrainLeafCell << 4))] = kGridLeafValue;
    require(lookupGridCell(kGridLevel4, 2, 1) == kTerrainParentCell,
            "lookupGridCell level 4 reads top-level grid buffer");
    require(lookupGridCell(kGridLevel3, 3, 4) == kTerrainParentCell,
            "lookupGridCell level 3 reads second-level grid buffer");
    require(lookupGridCell(kGridLevel2, 5, 10) == kTerrainParentCell,
            "lookupGridCell level 2 recurses through level 3 parent cell");
    require(lookupGridCell(kGridLevel1, 5, 5) == kTerrainLeafCell,
            "lookupGridCell level 1 recurses through level 2 parent cell");
    require(lookupGridCell(kGridLevel0, kGridLeafCol, kGridLeafRow) == kGridLeafValue,
            "lookupGridCell level 0 recurses through level 1 parent cell");
    require(lookupGridCell(kGridLevel1, kGridOutOfBoundsLow, 0) == -1 &&
                lookupGridCell(kGridLevel1, kGridOutOfBoundsHigh, 0) == -1,
            "lookupGridCell rejects original out-of-bounds coordinates");

    std::memset(objectTypeTable, 0, kObjectTypeTableBytes);
    std::memset(terrainTileCounts, 0, sizeof(struct TerrainCountTable) * kTerrainCountTableCount);
    terrainTilePtrs[kGridLevel1].entries[kTerrainLeafCell] = terrainTileBlock;
    terrainTileCounts[kGridLevel1].entries[kTerrainLeafCell] = 1;
    terrainTileBlock[0].buf3 = 0;
    terrainTileBlock[0].buf4 = 0;
    terrainTileBlock[0].buf5 = 0;
    terrainTileBlock[0].idx = kTerrainObjectType;
    objectTypeTable[kTerrainObjectType] = 1;
    struct NearestTerrain *nearest = findNearestTerrain(kTerrainWorldCoord, kTerrainWorldCoord);
    require(nearest != nullptr, "findNearestTerrain finds populated neighbor terrain");
    require(nearest->level == kGridLevel1 && nearest->gridX == 1 && nearest->gridY == 1 &&
                nearest->cellIdx == 0 && nearest->objectType == kTerrainObjectType,
            "findNearestTerrain records original level/cell/object metadata");
    require(nearest->worldX == kTerrainWorldCoord + kTerrainSubtileCenter &&
                nearest->worldY == kTerrainWorldCoord + kTerrainSubtileCenter &&
                nearest->dist == ((kTerrainSubtileCenter + kTerrainSubtileCenter) >> 2),
            "findNearestTerrain preserves original level-1 neighbor offset scaling");
    readItemSize = kTerrainExistingObjectSlot + 1;
    worldObjects[kTerrainExistingObjectSlot].x_coord = kTerrainFindResultX;
    worldObjects[kTerrainExistingObjectSlot].y_coord = kTerrainFindResultY;
    require(findOrPlaceItem(kTerrainFindInputX, kTerrainFindInputY, kTerrainPlacementSlot) ==
                kTerrainExistingObjectSlot,
            "findOrPlaceItem reuses an existing world object at the original nearest-terrain coordinate");
    worldObjects[kTerrainExistingObjectSlot].x_coord = 0;
    worldObjects[kTerrainExistingObjectSlot].y_coord = 0;
    require(findOrPlaceItem(kTerrainFindInputX, kTerrainFindInputY, kTerrainPlacementSlot) ==
                kTerrainPlacementSlot &&
                worldObjects[kTerrainPlacementSlot].x_coord == kTerrainFindResultX &&
                worldObjects[kTerrainPlacementSlot].y_coord == kTerrainFindResultY &&
                worldObjects[kTerrainPlacementSlot].objectIdx == kTerrainPlacementObjectIdx,
            "findOrPlaceItem places the original synthetic terrain object when no existing item matches");

    std::memset(gridBuf1, 0, kGridTopLevelBytes);
    std::memset(gridBuf2, 0, kGridLevel2Bytes);
    std::memset(gridBuf3, 0, kGridSubLevelBytes);
    std::memset(gridBuf4, 0, kGridSubLevelBytes);
    std::memset(gridBuf5, 0, kGridSubLevelBytes);
    std::memset(terrainTileCounts, 0, sizeof(struct TerrainCountTable) * kTerrainCountTableCount);
    gridBuf2[0] = kTerrainParentCell;
    gridBuf3[kTerrainParentCell << 4] = kTerrainLeafCell;
    terrainTilePtrs[kGridLevel2].entries[kTerrainLeafCell] = terrainTileBlock;
    terrainTileCounts[kGridLevel2].entries[kTerrainLeafCell] = 1;
    terrainTileBlock[0].buf3 = 0;
    terrainTileBlock[0].buf4 = 0;
    terrainTileBlock[0].buf5 = 0;
    terrainTileBlock[0].idx = kTerrainObjectType;
    nearest = findNearestTerrain(kTerrainWorldCoord, kTerrainWorldCoord);
    require(nearest != nullptr &&
                nearest->level == kGridLevel2 &&
                nearest->worldX == kTerrainWorldCoord + kTerrainLevel2ScaledOffset &&
                nearest->worldY == kTerrainWorldCoord + kTerrainLevel2ScaledOffset &&
                nearest->dist == kTerrainSubtileCenter,
            "findNearestTerrain preserves original level-2 neighbor offset scaling");

    std::memset(objectTypeTable, 0, kObjectTypeTableBytes);
    std::memset(terrainTileCounts, 0, sizeof(struct TerrainCountTable) * kTerrainCountTableCount);
    require(findNearestTerrain(kTerrainWorldCoord, kTerrainWorldCoord) == nullptr,
            "findNearestTerrain returns null when original terrain lookup finds no typed object");
    require(findOrPlaceItem(kTerrainFindInputX, kTerrainFindInputY, kTerrainPlacementSlot) == -1,
            "findOrPlaceItem preserves original failure when nearest terrain lookup is empty");

    std::memset(&game, 0, sizeof(game));
    std::strcpy(game.pilotName, "EAGLE ONE");
    game.totalScore = kPilotTotalScoreSample;
    game.lastScore = kPilotLastScoreSample;
    game.theater = kNorthCapeTheater;
    game.difficulty = kPilotDifficulty;
    game.rankHigh = kPilotRankHigh;
    game.rank = kPilotRankLow;
    game.medals = kPilotMedalSample;
    game.campaignProgress = kCampaignProgressRetired;
    struct Pilot pilot = {};
    gameDataToPilot(&pilot);
    require(std::strcmp(pilot.name, "EAGLE ONE") == 0,
            "gameDataToPilot copies the original nul-terminated pilot name");
    require(pilot.total_score == game.totalScore && pilot.last_score == game.lastScore &&
                pilot.theater == game.theater && pilot.difficulty == game.difficulty,
            "gameDataToPilot preserves original score, theater, and difficulty fields");
    require(static_cast<uint8>(pilot.rank) ==
                static_cast<uint8>((game.rankHigh << kRankHighShift) + game.rank),
            "gameDataToPilot packs original rankHigh and rank into one roster byte");
    require(pilot.medals == static_cast<uint8>(game.medals | kPilotRetiredBit),
            "gameDataToPilot maps campaignProgress 2 to the original retired medal bit");
    game.campaignProgress = kCampaignProgressDead;
    gameDataToPilot(&pilot);
    require(pilot.medals == static_cast<uint8>(game.medals | kPilotDeadBit),
            "gameDataToPilot maps campaignProgress 1 to the original dead medal bit");

    struct Pilot roster = {};
    std::strcpy(roster.name, "VIPER");
    roster.total_score = kRosterTotalScoreSample;
    roster.last_score = kRosterLastScoreSample;
    roster.rank = static_cast<int8>((kRosterRankHigh << kRankHighShift) | kRosterRankLow);
    roster.medals = static_cast<uint8>(kPilotDeadBit | kPilotRetiredBit | kRosterMedalSample);
    roster.theater = kDesertTheater;
    roster.difficulty = kRosterDifficulty;
    std::memset(&game, kPoisonByte, sizeof(game));
    selectedPilotIdx = kSelectedPilotIndex;
    pilotToGameData(reinterpret_cast<const uint8 *>(&roster));
    require(std::strcmp(game.pilotName, "VIPER") == 0,
            "pilotToGameData copies the original nul-terminated roster name");
    require(game.totalScore == roster.total_score && game.lastScore == roster.last_score,
            "pilotToGameData preserves original score fields from roster offsets");
    require(game.theater == kDesertTheater && game.difficulty == kRosterDifficulty,
            "pilotToGameData preserves original theater and difficulty bytes");
    require(game.rank == (roster.rank & kRankLowMask) &&
                game.rankHigh == (static_cast<uint8>(roster.rank) >> kRankHighShift),
            "pilotToGameData splits the original packed rank byte");
    require(game.medals == (roster.medals & kPilotMedalMask) &&
                game.campaignProgress == 0 && game.pilotIdx == kSelectedPilotIndex,
            "pilotToGameData masks medal bits and resets campaign progress like the original");

    resetStartRecorders();
    std::memset(&game, 0, sizeof(game));
    std::strcpy(game.pilotName, "ACE");
    game.hallOfFameEligible = 1;
    game.pilotIdx = kHallfameWritablePilotIdx;
    game.totalScore = kHallfameInsertScore;
    game.lastScore = kPilotLastScoreSample;
    game.theater = kLibyaTheater;
    game.difficulty = kPilotDifficulty;
    game.rankHigh = kPilotRankHigh;
    game.rank = kPilotRankLow;
    game.medals = kPilotMedalSample;
    for (int idx = 0; idx < HALLFAME_SLOTS; ++idx) {
        std::snprintf(hallfameBuf[idx].name, sizeof(hallfameBuf[idx].name), "P%d", idx);
        hallfameBuf[idx].total_score = kHallfameTopScore - idx * kHallfameScoreStep;
    }
    g_doFcbSearchResult = kWritableRoster;
    updateHallfame();
    require(selectedPilotIdx == kHallfameExpectedInsertIdx,
            "updateHallfame inserts eligible pilots after the last higher/equal score");
    require(std::strcmp(hallfameBuf[selectedPilotIdx].name, "ACE") == 0 &&
                hallfameBuf[selectedPilotIdx].total_score == kHallfameInsertScore,
            "updateHallfame stores current game data into the selected roster slot");
    require(hallfameBuf[selectedPilotIdx - 1].total_score == kHallfameHigherScore &&
                hallfameBuf[selectedPilotIdx + 1].total_score == kHallfameLowerScore,
            "updateHallfame shifts only the original range between insertion slot and old pilot slot");
    require(g_createCalls == kExpectedOneCall && g_lastCreateAttr == kHallfameCreateAttr &&
                g_fileWriteCalls == kHallfameSaveWriteCount && g_fileCloseCalls == kExpectedOneCall,
            "updateHallfame saves the HallFame file through the original write sequence");
    require(g_clearCalls == kExpectedNoCalls && g_commitCalls == kExpectedNoCalls &&
                g_flipCalls == kExpectedNoCalls,
            "updateHallfame skips warning UI when the roster is writable");

    resetStartRecorders();
    std::memset(&game, 0, sizeof(game));
    std::strcpy(game.pilotName, "ROOKIE");
    game.hallOfFameEligible = 0;
    game.pilotIdx = kHallfameReadOnlyPilotIdx;
    game.totalScore = kPilotTotalScoreSample;
    g_doFcbSearchResult = kReadOnlyOriginalDisk;
    updateHallfame();
    require(selectedPilotIdx == kHallfameReadOnlyPilotIdx && std::strcmp(hallfameBuf[selectedPilotIdx].name, "ROOKIE") == 0,
            "updateHallfame keeps the original pilot index when score is not hall-of-fame eligible");
    require(g_createCalls == kExpectedNoCalls && g_fileWriteCalls == kExpectedNoCalls,
            "updateHallfame does not save when the original-disk check fails");
    require(g_clearCalls == kExpectedOneCall && g_lastClearX1 == kWarningClearX1 &&
                g_lastClearY1 == kWarningClearY1 && g_lastClearX2 == kWarningClearX2 &&
                g_lastClearY2 == kWarningClearY2,
            "updateHallfame clears the full original warning page");
    require(g_commitCalls == kExpectedOneCall && g_flipCalls == kExpectedOneCall &&
                g_getKeyCalls == kExpectedOneCall && g_waitRetraceCalls == kExpectedOneCall,
            "updateHallfame shows and waits on the original read-only roster warning");
    require(g_centeredText.size() == kWarningTextCount && screenBuf[2] == kWarningEndColor,
            "updateHallfame emits the two original warning strings and restores gray text color");

    resetStartRecorders();
    loadHallfame();
    require(g_openFileCalls == kExpectedOneCall &&
                g_lastOpenName == "HallFame" &&
                g_lastOpenMode == 0 &&
                selectedPilotIdx == kHallfameReadSelectedIdx &&
                g_fileReadCalls == HALLFAME_SLOTS + 1 &&
                g_fileCloseCalls == kExpectedOneCall,
            "loadHallfame reads selected pilot plus all original HallFame roster records");
    require(std::strcmp(hallfameBuf[0].name, "L0") == 0 &&
                hallfameBuf[HALLFAME_SLOTS - 1].total_score ==
                    kHallfameReadBaseScore + HALLFAME_SLOTS - 1,
            "loadHallfame stores each original roster record in slot order");

    resetStartRecorders();
    selectedPilotIdx = kHallfameDisplayPilotIdx;
    std::memset(&hallfameBuf[kHallfameDisplayPilotIdx], 0, sizeof(hallfameBuf[kHallfameDisplayPilotIdx]));
    std::strcpy(hallfameBuf[kHallfameDisplayPilotIdx].name, "RAVEN");
    hallfameBuf[kHallfameDisplayPilotIdx].rank = kHallfameDisplayPilotRank;
    hallfameBuf[kHallfameDisplayPilotIdx].total_score = kHallfameDisplayPilotScore;
    hallfameBuf[kHallfameDisplayPilotIdx].last_score = kHallfameDisplayPilotLastScore;
    hallfameBuf[kHallfameDisplayPilotIdx].medals = kHallfameDisplayPilotMedals;
    printPilot(kHallfameDisplayPilotIdx);
    require(g_clearCalls == kExpectedOneCall &&
                g_lastClearX1 == kPilotColumnLeft &&
                g_lastClearY1 == kPilotTopMargin + kHallfameDisplayPilotIdx * kPilotRowHeight - 1 &&
                g_lastClearX2 == kPilotColumnLeft + kPilotEntryWidth,
            "printPilot clears the original pilot roster row rectangle");
    require(g_centeredText.size() == kHallfameDisplayTextCalls &&
                g_centeredText[0] == "Capt. RAVEN" &&
                g_centeredText[1] == "765,432 (1,234)",
            "printPilot draws original rank/name and score strings");
    require(g_showSpriteCalls == 2,
            "printPilot draws one original medal sprite for each set medal bit");
    require(g_lastSpriteX ==
                kPilotColumnLeft + (kPilotMedalCenterWidth - (kPilotFirstMedalWidth + 4 + kPilotThirdMedalWidth + 4)) / 2 +
                    kPilotFirstMedalWidth + 4 &&
                g_lastSpriteY == kPilotTopMargin + kHallfameDisplayPilotIdx * kPilotRowHeight + 17,
            "printPilot centers the original medal sprite row");
    require(g_lastSpriteWidth == kPilotThirdMedalWidth &&
                g_lastSpriteHeight == 16,
            "printPilot draws the original medal sprite size");

    resetStartRecorders();
    for (int idx = 0; idx < HALLFAME_SLOTS; ++idx) {
        std::memset(&hallfameBuf[idx], 0, sizeof(hallfameBuf[idx]));
        std::snprintf(hallfameBuf[idx].name, sizeof(hallfameBuf[idx].name), "D%d", idx);
    }
    displayPilots();
    require(g_clearCalls == kHallfameDisplayAllClearCalls &&
                g_centeredText.size() == kHallfameDisplayAllTextCalls &&
                g_centeredText.back() == "Use SELECTOR to choose pilot,  ESC to enter new pilot." &&
                g_commitCalls == kExpectedOneCall,
            "displayPilots prints all original roster rows, prompt text, and commits the page");

    resetStartRecorders();
    selectedPilotIdx = kHallfameDisplayPilotIdx;
    pilotSelectFlag = 0;
    blinkColorIdx = 0;
    blinkPilot();
    require(g_switchColorCalls == kExpectedNoCalls &&
                g_commitCalls == kExpectedNoCalls,
            "blinkPilot does nothing when the original pilot selection blink flag is clear");
    pilotSelectFlag = 1;
    blinkPilot();
    require(g_switchColorCalls == kExpectedOneCall &&
                g_lastSwitchX1 == kPilotColumnLeft &&
                g_lastSwitchY1 == kPilotTopMargin + kHallfameDisplayPilotIdx * kPilotRowHeight &&
                g_lastSwitchX2 == kPilotColumnLeft + kPilotEntryWidth &&
                g_lastSwitchY2 == kPilotTopMargin + kHallfameDisplayPilotIdx * kPilotRowHeight + kPilotNameHeight &&
                g_lastSwitchFrom == blinkColors[0] &&
                g_lastSwitchTo == blinkColors[1] &&
                blinkColorIdx == 1 &&
                g_commitCalls == kExpectedOneCall,
            "blinkPilot switches the original selected pilot name rectangle and toggles blink color");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    selectedPilotIdx = kPilotInputAcceptedIdx;
    hallfameBuf[selectedPilotIdx].medals = 0;
    pilotSelectFlag = 0;
    {
        const int keyStates[] = {kKeyBufferEmpty, kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kTwoStepScript);
    }
    g_getKeyResult = KEYCODE_ENTER;
    processPilotInput();
    require(selectedPilotIdx == kPilotInputAcceptedIdx &&
                pilotSelectFlag == 0 &&
                g_setTimerCalls == kExpectedOneCall &&
                g_restoreTimerCalls == kExpectedOneCall &&
                g_getKeyCalls == kExpectedOneCall,
            "processPilotInput accepts an eligible pilot on ENTER and restores the original timer handler");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    selectedPilotIdx = kPilotInputPreviousIdx;
    hallfameBuf[selectedPilotIdx].medals = 0;
    hallfameBuf[kPilotInputWrappedIdx].medals = 0;
    {
        const int keyStates[] = {
            kKeyBufferEmpty, kKeyBufferEmpty, kKeyBufferEmpty, kKeyBufferEmpty,
        };
        setKeyBufferScript(keyStates, 4);
    }
    {
        const int keyValues[] = {KEYCODE_UPARROW, KEYCODE_ENTER};
        setGetKeyScript(keyValues, kTwoStepScript);
    }
    processPilotInput();
    require(selectedPilotIdx == kPilotInputWrappedIdx &&
                g_switchColorCalls == 2 &&
                g_lastSwitchFrom == COLOR_GRAY &&
                g_lastSwitchTo == COLOR_WHITE,
            "processPilotInput preserves original arrow movement recolor before accepting a pilot");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    selectedPilotIdx = kPilotInputAcceptedIdx;
    for (int idx = 0; idx < HALLFAME_SLOTS; ++idx) hallfameBuf[idx].medals = 0;
    {
        const int keyStates[] = {
            kKeyBufferEmpty, kKeyBufferEmpty, kKeyBufferEmpty, kKeyBufferEmpty,
            kKeyBufferEmpty, kKeyBufferEmpty, kKeyBufferEmpty, kKeyBufferEmpty,
        };
        setKeyBufferScript(keyStates, kInputScriptCapacity);
    }
    {
        const int keyValues[] = {KEYCODE_DNARROW, KEYCODE_LEFTARROW, KEYCODE_RIGHTARROW, KEYCODE_ENTER};
        setGetKeyScript(keyValues, 4);
    }
    processPilotInput();
    require(selectedPilotIdx == kPilotInputPreviousIdx &&
                g_switchColorCalls == 6,
            "processPilotInput preserves original down/left/right pilot selector movement");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    selectedPilotIdx = kPilotInputAcceptedIdx;
    std::memset(&game, 0, sizeof(game));
    std::strcpy(game.pilotName, "NEW");
    hallfameBuf[selectedPilotIdx].medals = kPilotRetiredOrDeadMask;
    {
        const int keyStates[] = {
            kKeyBufferEmpty, kKeyBufferEmpty, kKeyBufferEmpty, kKeyBufferEmpty,
            kKeyBufferEmpty, kKeyBufferEmpty, kKeyBufferEmpty,
        };
        setKeyBufferScript(keyStates, 7);
    }
    {
        const int keyValues[] = {KEYCODE_ENTER, KEYCODE_ESC, KEYCODE_ENTER, KEYCODE_ENTER};
        setGetKeyScript(keyValues, 4);
    }
    processPilotInput();
    require(selectedPilotIdx == HALLFAME_SLOTS - 1 &&
                g_createCalls == kExpectedOneCall,
            "processPilotInput preserves original retired-pilot bell/ESC new-pilot insertion and save path");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    selectedPilotIdx = kPilotInputAcceptedIdx;
    {
        const int keyStates[] = {
            kKeyBufferEmpty, kKeyBufferEmpty, kKeyBufferEmpty,
            kKeyBufferEmpty, kKeyBufferEmpty,
        };
        setKeyBufferScript(keyStates, kPilotNameInputScriptCount);
    }
    {
        const int keyValues[] = {
            kPilotNameInputFirstChar,
            kPilotNameInputSecondChar,
            kPilotNameInputBackspace,
            kPilotNameInputThirdChar,
            KEYCODE_ENTER,
        };
        setGetKeyScript(keyValues, kPilotNameInputScriptCount);
    }
    Pilot namedPilot = {};
    int16 namePage[kDrawPageWordCount] = {};
    pilotNameInput(namePage, kPilotNameInputMaxLen, kPilotNameInputHeight,
                   kPilotNameInputClearHeight, &namedPilot);
    require(std::strcmp(namedPilot.name, "AC") == 0 &&
                g_getKeyCalls == kPilotNameInputScriptCount &&
                g_checkKeyCalls == kPilotNameInputScriptCount &&
                g_commitCalls == kPilotNameInputScriptCount,
            "pilotNameInput applies original clear, character append, backspace, and ENTER termination behavior");
    require(g_centeredText.size() == 1 &&
                g_centeredText[0] == "\376ENTER YOUR NAME !" &&
                g_lastClearX1 == kPilotNameInputPromptX1 &&
                g_lastClearY1 == kPilotNameInputPromptY1 &&
                g_lastClearX2 == kPilotNameInputPromptX2 &&
                g_lastClearY2 == kPilotNameInputPromptY2,
            "pilotNameInput draws and clears the original name-entry prompt area");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    {
        const int keyStates[] = {kKeyBufferPending, kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kTwoStepScript);
    }
    {
        const int keyValues[] = {KEYCODE_ENTER};
        setGetKeyScript(keyValues, kExpectedOneCall);
    }
    std::memset(&namedPilot, 0, sizeof(namedPilot));
    pilotNameInput(namePage, kPilotNameInputMaxLen, kPilotNameInputHeight,
                   kPilotNameInputClearHeight, &namedPilot);
    require(g_switchColorCalls == kExpectedOneCall &&
                g_commitCalls == kExpectedTwoCalls,
            "pilotNameInput preserves original cursor blink loop while waiting for key/joystick input");

    resetStartRecorders();
    std::memset(&game, 0, sizeof(game));
    comm.setupUseJoy = kKeyboardOnly;
    {
        const int keyStates[] = {kKeyBufferEmpty, kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kTwoStepScript);
    }
    g_getKeyResult = KEYCODE_ENTER;
    pilotSelect(kPilotSelectNeedSplash);
    require(std::strcmp(game.pilotName, "L3") == 0 &&
                game.pilotIdx == kHallfameReadSelectedIdx &&
                selectedPilotIdx == kHallfameReadSelectedIdx,
            "pilotSelect loads the original roster and copies the accepted selected pilot into gameData");
    require(g_waitRetraceCalls == kExpectedOneCall &&
                g_setFadeStepsCalls == 2 &&
                g_lastFadeSteps == kPilotSelectFadeStepSecond &&
                g_decodePicCalls == kExpectedOneCall &&
                g_showPicFileCalls == kExpectedOneCall &&
                g_setDacCalls == kExpectedOneCall &&
                g_lastDac == kPilotSelectDac &&
                g_flipCalls == kExpectedOneCall,
            "pilotSelect preserves the original splash/load/display/fade/DAC sequence when needSplash is set");
    require(g_openFileCalls == kPilotSelectPicOpenCount &&
                g_setTimerCalls == kExpectedOneCall &&
                g_restoreTimerCalls == kExpectedOneCall &&
                g_lastClearX1 == kWarningClearX1 &&
                g_lastClearY1 == kWarningClearY1 &&
                g_lastClearX2 == kWarningClearX2 &&
                g_lastClearY2 == kWarningClearY2,
            "pilotSelect accepts the pilot through the original input loop and clears the screen afterward");

    resetStartRecorders();
    std::memset(&game, 0, sizeof(game));
    game.hallOfFameEligible = 1;
    game.totalScore = kHallfameTopScore;
    game.pilotIdx = HALLFAME_SLOTS - 1;
    std::strcpy(game.pilotName, "ACE");
    comm.setupUseJoy = kKeyboardOnly;
    {
        const int keyStates[] = {kKeyBufferEmpty, kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kTwoStepScript);
    }
    g_getKeyResult = KEYCODE_ENTER;
    pilotSelect(0);
    require(g_createCalls == kExpectedOneCall &&
                std::strcmp(game.pilotName, "ACE") == 0,
            "pilotSelect with needSplash clear updates the original HallFame before accepting pilot input");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    {
        const int keyStates[] = {kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kExpectedOneCall);
    }
    require(getJoyKey() == kExpectedOneCall && g_joystickCalls == kExpectedNoCalls &&
                g_checkKeyCalls == kExpectedOneCall,
            "getJoyKey returns true when keyboard-only mode sees an empty original key buffer");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    {
        const int keyStates[] = {kKeyBufferPending};
        setKeyBufferScript(keyStates, kExpectedOneCall);
    }
    require(getJoyKey() == kExpectedNoCalls,
            "getJoyKey returns false when keyboard-only mode sees a pending original key");

    resetStartRecorders();
    comm.setupUseJoy = kJoystickEnabled;
    {
        const int joystickStates[] = {kJoystickPressed};
        setJoystickScript(joystickStates, kExpectedOneCall);
    }
    require(getJoyKey() == kExpectedOneCall && g_joystickCalls == kExpectedOneCall &&
                g_checkKeyCalls == kExpectedNoCalls,
            "getJoyKey accepts an immediate joystick press before checking the keyboard buffer");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    cbreakHit = 1;
    {
        const pid_t child = fork();
        require(child >= 0, "test should be able to fork for getJoyKey C-break exit behavior");
        if (child == 0) {
            getJoyKey();
            std::exit(kTestFailureExitCode);
        }
        int status = 0;
        require(waitpid(child, &status, 0) == child,
                "parent should wait for getJoyKey C-break child");
        require(WIFEXITED(status) && WEXITSTATUS(status) == kChildExitOk,
                "getJoyKey preserves original cleanup/restore exit path on C-break");
    }
    cbreakHit = 0;

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    g_getKeyResult = kKeyboardSampleKey;
    require(readInputKey() == kKeyboardSampleKey && g_getKeyCalls == kExpectedOneCall,
            "readInputKey returns the original keyboard value in keyboard-only mode");

    resetStartRecorders();
    comm.setupUseJoy = kJoystickEnabled;
    {
        const int keyStates[] = {kKeyBufferPending, kKeyBufferPending};
        const int joystickStates[] = {kJoystickPressed};
        setKeyBufferScript(keyStates, kTwoStepScript);
        setJoystickScript(joystickStates, kExpectedOneCall);
    }
    require(readInputKey() == KEYCODE_ENTER && g_getKeyCalls == kExpectedNoCalls &&
                g_joystickCalls == kExpectedOneCall,
            "readInputKey maps a joystick selector press to the original ENTER keycode");

    resetStartRecorders();
    comm.setupUseJoy = kJoystickEnabled;
    {
        const int keyStates[] = {kKeyBufferPending, kKeyBufferEmpty};
        const int joystickStates[] = {kJoystickIdle};
        setKeyBufferScript(keyStates, kTwoStepScript);
        setJoystickScript(joystickStates, kExpectedOneCall);
    }
    g_getKeyResult = kKeyboardSampleKey;
    require(readInputKey() == kKeyboardSampleKey && g_getKeyCalls == kExpectedOneCall &&
                g_joystickCalls == kExpectedOneCall,
            "readInputKey falls through to keyboard when joystick mode sees an empty key buffer");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    g_getKeyResult = KEYCODE_ALTQ;
    {
        const pid_t child = fork();
        require(child >= 0, "test should be able to fork for readInputKey Alt-Q exit behavior");
        if (child == 0) {
            readInputKey();
            std::exit(kTestFailureExitCode);
        }
        int status = 0;
        require(waitpid(child, &status, 0) == child,
                "parent should wait for readInputKey Alt-Q child");
        require(WIFEXITED(status) && WEXITSTATUS(status) == kChildExitOk,
                "readInputKey preserves original cleanup exit path on Alt-Q");
    }

    resetStartRecorders();
    {
        const int keyStates[] = {kKeyBufferEmpty, kKeyBufferEmpty, kKeyBufferPending};
        setKeyBufferScript(keyStates, kClearKeybufCheckCalls);
    }
    clearKeybuf();
    require(g_checkKeyCalls == kClearKeybufCheckCalls && g_getKeyCalls == kClearKeybufGetKeyCalls,
            "clearKeybuf consumes original pending-key sentinel values until the buffer state changes");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    {
        const int keyStates[] = {kKeyBufferPending};
        setKeyBufferScript(keyStates, kExpectedOneCall);
    }
    require(joyOrKey() == kExpectedNoCalls && g_getKeyCalls == kExpectedNoCalls,
            "joyOrKey returns false when the original key-buffer check says no key is ready");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    {
        const int keyStates[] = {kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kExpectedOneCall);
    }
    g_getKeyResult = kKeyboardSampleKey;
    require(joyOrKey() == kExpectedOneCall && g_getKeyCalls == kExpectedOneCall,
            "joyOrKey consumes a keyboard key and returns true when the original buffer check is zero");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    {
        const int keyStates[] = {kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kExpectedOneCall);
    }
    g_getKeyResult = KEYCODE_ALTQ;
    {
        const pid_t child = fork();
        require(child >= 0, "test should be able to fork for joyOrKey Alt-Q exit behavior");
        if (child == 0) {
            joyOrKey();
            std::exit(kTestFailureExitCode);
        }
        int status = 0;
        require(waitpid(child, &status, 0) == child,
                "parent should wait for joyOrKey Alt-Q child");
        require(WIFEXITED(status) && WEXITSTATUS(status) == kChildExitOk,
                "joyOrKey preserves original cleanup exit path on Alt-Q");
    }

    resetStartRecorders();
    comm.setupUseJoy = kJoystickEnabled;
    {
        const int joystickStates[] = {kJoystickPressed};
        setJoystickScript(joystickStates, kExpectedOneCall);
    }
    require(joyOrKey() == kExpectedOneCall && g_joystickCalls == kExpectedOneCall &&
                g_checkKeyCalls == kExpectedNoCalls,
            "joyOrKey accepts joystick selector input before checking the keyboard buffer");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    cbreakHit = 1;
    {
        const pid_t child = fork();
        require(child >= 0, "test should be able to fork for joyOrKey C-break exit behavior");
        if (child == 0) {
            joyOrKey();
            std::exit(kTestFailureExitCode);
        }
        int status = 0;
        require(waitpid(child, &status, 0) == child,
                "parent should wait for joyOrKey C-break child");
        require(WIFEXITED(status) && WEXITSTATUS(status) == kChildExitOk,
                "joyOrKey preserves original cleanup/restore exit path on C-break");
    }
    cbreakHit = 0;

    resetStartRecorders();
    {
        const int16 drawPage = kDrawLinePage;
        drawLine(&drawPage, kDrawLineX1, kDrawLineY1, kDrawLineX2, kDrawLineY2, kDrawLineColor);
    }
    require(g_setPageNCalls == kExpectedOneCall && g_lastSetPageN == kDrawLinePage &&
                g_setColorCalls == kExpectedOneCall && g_lastSetColor == kDrawLineColor,
            "drawLine selects the original page and color before drawing");
    require(lineX1 == kDrawLineX1 && lineY1 == kDrawLineY1 &&
                lineX2 == kDrawLineX2 && lineY2 == kDrawLineY2 &&
                g_lineWrapperCalls == kExpectedOneCall && g_nop23Calls == kExpectedOneCall,
            "drawLine writes the original global line endpoints and finalizes the draw call");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    {
        const int keyStates[] = {kKeyBufferPending, kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kTwoStepScript);
    }
    g_getKeyResult = kKeyboardSampleKey;
    waitJoyKey();
    require(g_checkKeyCalls == kTwoStepScript &&
                g_getKeyCalls == kExpectedOneCall,
            "waitJoyKey spins through joyOrKey until the original key-buffer-ready state is observed");

    resetStartRecorders();
    showPic640("TITLE640.PIC");
    require(g_intDispatchCalls == kExpectedOneCall &&
                g_lastInterrupt == kVideoInterrupt &&
                intRegs[1] == kVideoModeSetFunction &&
                intRegs[0] == kVideoMode640x350,
            "showPic640 sets the original 640x350 video mode through INT 10h");
    require(g_setDacCalls == kExpectedOneCall &&
                g_lastDac == kShowPic640Dac &&
                g_openFileCalls == kExpectedOneCall &&
                g_lastOpenName == "TITLE640.PIC" &&
                g_lastOpenMode == 0 &&
                g_picBlitCalls == kExpectedOneCall &&
                g_lastPicBlitPage == kShowPic640Page &&
                g_lastPicBlitHandle == reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kFakeOpenFileHandle)) &&
                g_fileCloseCalls == kExpectedOneCall,
            "showPic640 preserves the original open-blit-close picture chain and DAC setup");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    {
        const int keyStates[] = {kKeyBufferEmpty, kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kTwoStepScript);
    }
    g_getKeyResult = kKeyboardScanCodeSample;
    require(pollMenuInput() == kKeyboardScanCodeLowByte &&
                g_checkKeyCalls == kPollMenuKeyboardCheckCalls &&
                g_getKeyCalls == kExpectedOneCall,
            "pollMenuInput masks keyboard scan codes to the original low byte when present");

    resetStartRecorders();
    comm.setupUseJoy = kJoystickEnabled;
    {
        const int keyStates[] = {kKeyBufferPending, kKeyBufferPending};
        const int joystickStates[] = {kJoystickPressed, kJoystickIdle};
        setKeyBufferScript(keyStates, kTwoStepScript);
        setJoystickScript(joystickStates, kTwoStepScript);
    }
    require(pollMenuInput() == KEYCODE_ENTER,
            "pollMenuInput maps joystick button 0 to the original ENTER action");
    require(g_pollJoystickCalls == kPollJoystickAxesCalls,
            "pollMenuInput polls joystick axes once for the immediate button action");

    resetStartRecorders();
    comm.setupUseJoy = kJoystickEnabled;
    {
        const int keyStates[] = {kKeyBufferPending, kKeyBufferPending};
        const int joystickStates[] = {kJoystickIdle, kJoystickIdle};
        setKeyBufferScript(keyStates, kTwoStepScript);
        setJoystickScript(joystickStates, kTwoStepScript);
    }
    g_polledJoyY = kJoystickAxisUp;
    require(pollMenuInput() == KEYCODE_UPARROW && joyRepeatFlag == kExpectedOneCall,
            "pollMenuInput maps an upward joystick axis to the original UP scan code and repeat flag");

    resetStartRecorders();
    comm.setupUseJoy = kJoystickEnabled;
    {
        const int keyStates[] = {kKeyBufferPending, kKeyBufferPending};
        const int joystickStates[] = {kJoystickIdle, kJoystickIdle};
        setKeyBufferScript(keyStates, kTwoStepScript);
        setJoystickScript(joystickStates, kTwoStepScript);
    }
    g_polledJoyY = JOY_DEADZONE_HI + 1;
    require(pollMenuInput() == KEYCODE_DNARROW && joyRepeatFlag == kExpectedOneCall,
            "pollMenuInput maps a downward joystick axis to the original DOWN scan code and repeat flag");

    resetStartRecorders();
    comm.setupUseJoy = kJoystickEnabled;
    {
        const int keyStates[] = {kKeyBufferPending, kKeyBufferPending};
        const int joystickStates[] = {kJoystickIdle, kJoystickIdle};
        setKeyBufferScript(keyStates, kTwoStepScript);
        setJoystickScript(joystickStates, kTwoStepScript);
    }
    g_polledJoyX = JOY_DEADZONE_LO - 1;
    require(pollMenuInput() == KEYCODE_LEFTARROW && joyRepeatFlag == kExpectedOneCall,
            "pollMenuInput maps a left joystick axis to the original LEFT scan code and repeat flag");

    resetStartRecorders();
    comm.setupUseJoy = kJoystickEnabled;
    {
        const int keyStates[] = {kKeyBufferPending, kKeyBufferPending};
        const int joystickStates[] = {kJoystickIdle, kJoystickIdle};
        setKeyBufferScript(keyStates, kTwoStepScript);
        setJoystickScript(joystickStates, kTwoStepScript);
    }
    g_polledJoyX = JOY_DEADZONE_HI + 1;
    require(pollMenuInput() == KEYCODE_RIGHTARROW && joyRepeatFlag == kExpectedOneCall,
            "pollMenuInput maps a right joystick axis to the original RIGHT scan code and repeat flag");

    resetStartRecorders();
    comm.setupUseJoy = kJoystickEnabled;
    joyRepeatFlag = kExpectedOneCall;
    g_timerCounterStepOnKeyCheck = kPollMenuRepeatTimerStep;
    g_getKeyResult = kKeyboardSampleKey;
    require(pollMenuInput() == kKeyboardScanCodeLowByte &&
                joyRepeatFlag == 0 &&
                g_checkKeyCalls == kPollMenuRepeatCheckCalls &&
                g_joystickCalls == kPollMenuRepeatJoystickReads &&
                g_pollJoystickCalls == kPollMenuRepeatPollJoystickCalls,
            "pollMenuInput preserves original joystick-repeat hold delay before accepting keyboard input");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    cbreakHit = 1;
    {
        const int keyStates[] = {kKeyBufferPending};
        setKeyBufferScript(keyStates, kExpectedOneCall);
    }
    {
        const pid_t child = fork();
        require(child >= 0, "test should be able to fork for pollMenuInput C-break exit behavior");
        if (child == 0) {
            pollMenuInput();
            std::exit(kTestFailureExitCode);
        }
        int status = 0;
        require(waitpid(child, &status, 0) == child,
                "parent should wait for pollMenuInput C-break child");
        require(WIFEXITED(status) && WEXITSTATUS(status) == kChildExitOk,
                "pollMenuInput preserves original cleanup/restore exit path on C-break while waiting");
    }
    cbreakHit = 0;

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    {
        const int keyStates[] = {kKeyBufferEmpty, kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kTwoStepScript);
    }
    g_getKeyResult = KEYCODE_ALTQ;
    {
        const pid_t child = fork();
        require(child >= 0, "test should be able to fork for pollMenuInput Alt-Q exit behavior");
        if (child == 0) {
            pollMenuInput();
            std::exit(kTestFailureExitCode);
        }
        int status = 0;
        require(waitpid(child, &status, 0) == child,
                "parent should wait for pollMenuInput Alt-Q child");
        require(WIFEXITED(status) && WEXITSTATUS(status) == kChildExitOk,
                "pollMenuInput preserves original cleanup/restore exit path on Alt-Q");
    }

    resetStartRecorders();
    clearBriefing();
    require(g_clearCalls == kExpectedOneCall && g_lastClearX1 == kClearBriefingX1 &&
                g_lastClearY1 == kClearBriefingY1 && g_lastClearX2 == kClearBriefingX2 &&
                g_lastClearY2 == kClearBriefingY2,
            "clearBriefing clears the original briefing board rectangle");

    resetStartRecorders();
    g_getKeyResult = kRepeatNoKey;
    timerCounter3 = kAnimateArmReadyCounter;
    armPosition = kGridLevel0;
    require(askRepeatMission() == kExpectedNoCalls,
            "askRepeatMission returns false for non-yes keys");
    require(g_centeredText.size() == kExpectedOneCall &&
                g_centeredText[0] == "Repeat last mission ? (y/n)" &&
                page1Desc.color == kRepeatPromptColor &&
                g_getKeyCalls == kExpectedOneCall,
            "askRepeatMission draws the original prompt and reads one key");
    require(g_clearCalls == kExpectedOneCall && g_showSpriteCalls == kExpectedOneCall &&
                g_blitToCurrentCalls == kExpectedOneCall,
            "askRepeatMission runs the original arm animation before clearing the briefing");

    resetStartRecorders();
    g_getKeyResult = kRepeatYesKey;
    timerCounter3 = kAnimateArmReadyCounter;
    armPosition = kGridLevel0;
    require(askRepeatMission() == kExpectedOneCall,
            "askRepeatMission accepts uppercase Y like the original");

    resetStartRecorders();
    timerCounter3 = kAnimateArmReadyCounter;
    armPosition = kGridLevel0;
    missionDecode();
    require(g_centeredText.size() == kExpectedOneCall &&
                g_centeredText[0] == "decoding mission..." &&
                page1Desc.color == kMissionDecodeColor && enableHighlight == kExpectedNoCalls,
            "missionDecode draws the original status text and disables highlight");
    require(g_showSpriteCalls == kExpectedOneCall && g_blitToCurrentCalls == kExpectedOneCall,
            "missionDecode runs the original arm animation");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    {
        const int keyStates[] = {kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kExpectedOneCall);
    }
    g_getKeyResult = kKeyboardSampleKey;
    timerCounter3 = kAnimateArmReadyCounter;
    timerCounter = 0;
    armPosition = kGridLevel0;
    targets[0].baseIdx = kPrintMissionBaseIdx;
    targets[0].targetIdx = kPrintMissionPrimaryIdx;
    std::strcpy(targets[0].coord, "P1A2");
    targets[1].targetIdx = kPrintMissionSecondaryIdx;
    std::strcpy(targets[1].coord, "S3B4");
    worldObjects[kPrintMissionBaseIdx].objectIdx = kPrintMissionBaseObjectName;
    worldObjects[kPrintMissionBaseIdx].unitRef = 0;
    worldObjects[kPrintMissionBaseIdx].x_coord = kPrintMissionBaseWorldX;
    worldObjects[kPrintMissionBaseIdx].y_coord = kPrintMissionBaseWorldY;
    worldObjects[kPrintMissionPrimaryIdx].objectIdx = kPrintMissionPrimaryObjectName;
    worldObjects[kPrintMissionPrimaryIdx].unitRef = 0;
    worldObjects[kPrintMissionSecondaryIdx].objectIdx = kPrintMissionSecondaryObjectName;
    worldObjects[kPrintMissionSecondaryIdx].unitRef = 0;
    wldOffsets[0] = const_cast<char *>("");
    wldOffsets[kPrintMissionBaseObjectName] = const_cast<char *>("Carrier");
    wldOffsets[kPrintMissionPrimaryObjectName] = const_cast<char *>("Radar");
    wldOffsets[kPrintMissionSecondaryObjectName] = const_cast<char *>("Depot");
    game.theater = kLibyaTheater;
    printMission();
    require(g_clearCalls == kExpectedOneCall &&
                g_centeredText.size() == kPrintMissionCenteredTextCount &&
                g_centeredText[0] == "TODAY'S MISSION" &&
                g_centeredText[1] == "Carrier" &&
                g_centeredText[2] ==
                    std::string("ONC ") +
                        expectedGridRef(kPrintMissionBaseWorldX, kPrintMissionBaseWorldY, kLibyaTheater) &&
                g_centeredText[3] == "Radar" &&
                g_centeredText[4] == "ONC P1A2" &&
                g_centeredText[5] == "Depot" &&
                g_centeredText[6] == "ONC S3B4",
            "printMission draws the original centered mission title, target labels, and ONC coordinates");
    require(g_drawCalls.size() == kPrintMissionDrawCallCount &&
                g_drawCalls[0].text == "Takeoff from:" &&
                g_drawCalls[0].y == kPrintMissionTakeoffY &&
                g_drawCalls[1].text == "Primary Target:" &&
                g_drawCalls[1].y == kPrintMissionPrimaryY &&
                g_drawCalls[2].text == "Secondary Target:" &&
                g_drawCalls[2].y == kPrintMissionSecondaryY,
            "printMission draws the original left-aligned briefing section labels");
    require(g_setTimerCalls == kExpectedOneCall &&
                g_restoreTimerCalls == kExpectedOneCall &&
                enableHighlight == kExpectedOneCall &&
                g_getKeyCalls == kExpectedOneCall,
            "printMission brackets animation with the original timer handler and exits on selector input");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    {
        const int keyStates[] = {kKeyBufferPending, kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kPrintMissionDelayedKeyBufferChecks);
    }
    g_getKeyResult = kKeyboardSampleKey;
    timerCounter3 = kAnimateArmReadyCounter;
    g_timerCounterStepOnKeyCheck = kPrintMissionTimedStep;
    armPosition = kGridLevel0;
    printMission();
    require(g_checkKeyCalls == kPrintMissionDelayedKeyBufferChecks &&
                g_getKeyCalls == kExpectedOneCall &&
                g_showSpriteCalls > kExpectedTwoCalls,
            "printMission advances the original timed arm animation while waiting for selector input");

    resetStartRecorders();
    checkDiskA();
    require(g_openFileCalls == kExpectedOneCall && g_lastOpenName == "F15.spr" &&
                g_lastOpenMode == kF15SprOpenMode && g_fileCloseCalls == kExpectedOneCall,
            "checkDiskA opens and closes the original F15.spr disk-check file");
    require(g_clearCalls == kExpectedOneCall && g_centeredText.empty(),
            "checkDiskA clears the briefing immediately when the disk check succeeds");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    g_openFileNullResponses = kDiskMissingOpenFailures;
    {
        const int keyStates[] = {kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kExpectedOneCall);
    }
    g_getKeyResult = kKeyboardSampleKey;
    checkDiskA();
    require(g_openFileCalls == kExpectedTwoCalls &&
                g_centeredText.size() == kExpectedTwoCalls &&
                g_centeredText[0] == "Please reinsert F15 Disk A" &&
                g_centeredText[1] == "<Press selector when ready>",
            "checkDiskA preserves original missing-disk prompt before retrying F15.spr");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    for (int idx = 0; idx < kMissionMenuItemCount; ++idx) scenarioFoundArr[idx] = 0;
    {
        const int keyStates[] = {kKeyBufferEmpty, kKeyBufferEmpty};
        setKeyBufferScript(keyStates, kTwoStepScript);
    }
    g_getKeyResult = KEYCODE_ENTER;
    {
        const char *names[kMissionMenuItemCount] = {"N0", "N1", "N2", "N3", "N4"};
        const char *desc[kMissionMenuItemCount] = {"D0", "D1", "D2", "D3", "D4"};
        require(missionMenuSelect(names, desc, "TITLE", kMissionMenuSelection) == kMissionMenuSelection,
                "missionMenuSelect returns the accepted original selection");
    }
    require(g_setTimerCalls == kExpectedOneCall && g_restoreTimerCalls == kExpectedOneCall,
            "missionMenuSelect brackets input with the original timer IRQ handler calls");
    require(g_centeredText.size() == kMissionMenuTextCount && g_centeredText[0] == "TITLE",
            "missionMenuSelect draws the original title plus five name/description rows");
    require(g_clearCalls == kExpectedOneCall && enableHighlight == kExpectedOneCall,
            "missionMenuSelect clears the briefing and leaves highlight enabled after accepting");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    for (int idx = 0; idx < kMissionMenuItemCount; ++idx) scenarioFoundArr[idx] = 0;
    {
        const int keyStates[] = {
            kKeyBufferEmpty, kKeyBufferEmpty, kKeyBufferEmpty,
            kKeyBufferEmpty, kKeyBufferEmpty, kKeyBufferEmpty,
        };
        setKeyBufferScript(keyStates, 6);
    }
    {
        const int keyValues[] = {KEYCODE_UPARROW, KEYCODE_DNARROW, KEYCODE_ENTER};
        setGetKeyScript(keyValues, kThreeStepScript);
    }
    {
        const char *names[kMissionMenuItemCount] = {"N0", "N1", "N2", "N3", "N4"};
        const char *desc[kMissionMenuItemCount] = {"D0", "D1", "D2", "D3", "D4"};
        require(missionMenuSelect(names, desc, "TITLE", 1) == 1,
                "missionMenuSelect preserves original up/down selector movement before accepting");
    }

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    game.difficulty = kMissionSelectDifficulty;
    game.theater = kMissionSelectTheater;
    missionPick = -1;
    {
        const int keyStates[] = {
            kKeyBufferEmpty, kKeyBufferEmpty,
            kKeyBufferEmpty, kKeyBufferEmpty,
        };
        setKeyBufferScript(keyStates, kMissionSelectKeyBufferChecks);
    }
    {
        const int keyValues[] = {KEYCODE_ENTER, KEYCODE_ENTER};
        setGetKeyScript(keyValues, kMissionSelectMenuCount);
    }
    missionSelect();
    require(game.difficulty == kMissionSelectDifficulty &&
                game.theater == kMissionSelectTheater &&
                missionPick == -1,
            "missionSelect preserves accepted original difficulty/theater on the ordinary non-scenario path");
    require(g_setDacCalls == kExpectedOneCall &&
                g_lastDac == kPilotSelectDac &&
                g_setFadeStepsCalls == kExpectedOneCall &&
                g_lastFadeSteps == kMissionSelectFadeSteps &&
                g_openFileCalls == kMissionSelectOpenCount &&
                g_showPicFileCalls == kExpectedOneCall,
            "missionSelect preserves the original wall-picture, disk-check, DAC, and fade setup sequence");
    require(g_getKeyCalls == kMissionSelectMenuCount &&
                g_setTimerCalls == kMissionSelectMenuCount &&
                g_restoreTimerCalls == kMissionSelectMenuCount,
            "missionSelect accepts both original menus through missionMenuSelect");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    game.difficulty = kMissionSelectDifficulty;
    game.theater = kInvalidTheater;
    missionPick = -1;
    {
        int keyStates[kMissionSelectScenarioKeyBufferChecks] = {};
        setKeyBufferScript(keyStates, kMissionSelectScenarioKeyBufferChecks);
    }
    {
        const int keyValues[] = {
            KEYCODE_ENTER,    // difficulty menu
            KEYCODE_ENTER,    // clamped Other theater menu entry
            KEYCODE_DNARROW,  // scenario menu: North Cape -> Central Europe
            KEYCODE_DNARROW,  // scenario menu: Central Europe -> Desert Storm
            KEYCODE_ENTER,    // accept Desert Storm scenario
            KEYCODE_ENTER,    // mission type: Historical Missions
            KEYCODE_ENTER,    // historical page 1: more historical missions
            KEYCODE_UPARROW,  // historical page 2: move from "more" to Republican Guards
            KEYCODE_ENTER,    // accept historical mission 7
        };
        setGetKeyScript(keyValues, kMissionSelectScenarioKeyActions);
    }
    missionSelect();
    require(game.theater == THEATER_DS &&
                missionPick == 7 &&
                g_openFileCalls == kMissionSelectScenarioOpenCount,
            "missionSelect preserves original Other-scenario probing and Desert Storm historical selection");
    require(g_setTimerCalls == kMissionSelectScenarioMenuCount &&
                g_restoreTimerCalls == kMissionSelectScenarioMenuCount,
            "missionSelect drives each original scenario/type/history menu through missionMenuSelect");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    game.difficulty = kMissionSelectDifficulty;
    game.theater = THEATER_OTHER;
    missionPick = -1;
    {
        int keyStates[kMissionSelectScenarioCancelKeyBufferChecks] = {};
        setKeyBufferScript(keyStates, kMissionSelectScenarioCancelKeyBufferChecks);
    }
    {
        const int keyValues[] = {
            KEYCODE_ENTER,   // difficulty menu
            KEYCODE_ENTER,   // first theater menu accepts Other
            KEYCODE_DNARROW, // scenario menu: move toward "previous theater screen"
            KEYCODE_DNARROW,
            KEYCODE_DNARROW,
            KEYCODE_DNARROW,
            KEYCODE_ENTER,   // scenario menu: selection 4 makes theater 8 and retries
            KEYCODE_UPARROW, // retry theater menu: Other -> Europe
            KEYCODE_ENTER,   // accept built-in Europe theater
        };
        setGetKeyScript(keyValues, kMissionSelectScenarioCancelKeyActions);
    }
    missionSelect();
    require(game.theater == kEuropeTheater &&
                missionPick == -1 &&
                g_openFileCalls == kMissionSelectScenarioCancelOpenCount,
            "missionSelect preserves original Other-scenario cancel back to theater selection");
    require(g_setTimerCalls == kMissionSelectScenarioCancelMenuCount &&
                g_restoreTimerCalls == kMissionSelectScenarioCancelMenuCount,
            "missionSelect retries the original theater menu after scenario selection 8");

    resetStartRecorders();
    comm.setupUseJoy = kKeyboardOnly;
    game.difficulty = kMissionSelectDifficulty;
    game.theater = THEATER_OTHER;
    missionPick = -1;
    g_nullOpenNames = {"nc.3d3", "ce.3d3", "jp.3d3", "na.3d3"};
    {
        int keyStates[kMissionSelectNoScenarioKeyBufferChecks] = {};
        setKeyBufferScript(keyStates, kMissionSelectNoScenarioKeyBufferChecks);
    }
    {
        const int keyValues[] = {
            KEYCODE_ENTER,   // difficulty menu
            KEYCODE_ENTER,   // first theater menu accepts Other
            KEYCODE_ENTER,   // waitJoyKey after "No scenario files found"
            KEYCODE_UPARROW, // retry theater menu: Other -> Europe
            KEYCODE_ENTER,   // accept built-in Europe theater
        };
        setGetKeyScript(keyValues, kMissionSelectNoScenarioKeyActions);
    }
    missionSelect();
    require(game.theater == kEuropeTheater &&
                missionPick == -1 &&
                g_openFileCalls == kMissionSelectNoScenarioOpenCount,
            "missionSelect preserves original no-extra-scenario message and returns to built-in theater selection");
    bool sawNoScenarioMessage = false;
    bool sawSupplementMessage = false;
    for (const std::string &text : g_centeredText) {
        if (text == "No scenario files found") sawNoScenarioMessage = true;
        if (text == "See Technical Supplement") sawSupplementMessage = true;
    }
    require(sawNoScenarioMessage && sawSupplementMessage,
            "missionSelect draws the original missing-scenario instructions before retrying");

    std::cout << "start_behavior_tests passed\n";
    return 0;
}
