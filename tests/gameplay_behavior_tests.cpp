#include "egdata.h"
#include "egmath.h"
#include "inttype.h"
#include "struct.h"
#include "dos.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

extern int computeThreatRangeBearing(int threatX, int threatY, int threatAlt, int threatType,
                                     int *outBearing, int *outRange);
extern int computeThreatScore(void);
extern int samCanAcquireTarget(int slot, int targetX, int targetY, int targetAlt, int mode);
extern int markTargetReached(int targetIdx);
extern void buildRangeString(int rangeRaw);
extern int rangeApprox(int deltaX, int deltaY);
extern void appendMapEvent(int eventType, int eventArg);
extern void scheduleTimedEvent(int keyVal, int delay);
extern void scheduleEventCheck(int eventObjIdx, unsigned int priority);
extern void placeString(int waypointIdx);
extern void finalizeMission(int outcome);
extern void moveNearFar(void *nearPtr, int count);
extern int setCommWorldbufPtr();
extern void recalcTimeScale(void);
extern void setupLodDistances(void);
extern void exitSlowMotion(void);
extern void updateEngineSound(void);
extern void makeSound(int soundId, int priority);
extern void playVoiceCue(int weaponIdx);
extern void selectMissile(void);
extern void applyGravityFall(void);
extern void resetSimObjectLocks(void);
extern void initWeaponLoadout(void);
extern void countermeasures(int eventType);
extern void tickMessageTimers(void);
extern void updateBulletsAndFire(void);
extern void updateTracerParticles(void);
extern void initFrameRandom(void);
extern void drawWeaponAmmo(void);
extern void generateRandomRadioMessage(void);
extern void dispatchKeyScancode(void);
extern void updateFrame(void);
extern void initMissionStrings(void);
extern void findWaypointFeatures(void);
extern void moveDataFar(void);
extern void moveStuff(void);
extern void spawnEnemyAircraft(int slot, int objType);
extern void destroyAircraft(int objIdx);
extern void bombTarget(void);
extern void fireMissile(void);
extern void keyDispatch(uint16 scanCode);
extern void updateTargetLock(void);
extern void drawHudWorldOverlay(void);
extern void drawTargetBox(int centerX, int centerY, int size, int mode);
extern void drawMissileLock(void);
extern void drawTargetLabel(const char *text, int color, int size);
extern void updateThreatAlert(void);
extern void updateThreatSites(void);
extern void fireGroundThreat(int planeIdx);
extern void updateObjects(void);
extern void fireAirThreat(int objIdx);
extern void updateThreatTargeting(void);
extern void destroyGroundTarget(int planeIdx);
extern void testWorldPosVisible(int worldX, int worldY, int worldZ);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum GameplayOriginalConstant : int {
    kAngleQuarterTurn = 0x4000,
    kAngleHalfTurn = 0x8000,
    kSamFrontCone = 0x2000,
    kSamTurnCone = 0x1000,
    kSamRearAspect = 0x4000,
    kProjectileSlot = 0,
    kPrimaryTargetFlag = 0x4000,
    kSecondaryTargetFlag = 0x2000,
    kPrimaryWaypoint = 2,
    kSecondaryWaypoint = 1,
    kBothTargetsWaypoint = 3,
    kThreatAltitudeBias = 0x40,
    kThreatAltitudeShift = 6,
    kThreatScoreShift = 7,
    kThreatLethalityDivisor = 16,
    kThreatScoreDivisor = 64,
    kThreatTotalDivisor = 100,
    kRangeKmShift = 6,
    kRangeFractionScale = 2,
    kRangeFractionDivisor = 13,
    kDirectorKeyValue = 0x89,
    kReplayEventTerminator = 0,
    kReplayEventLimit = 255,
    kLandingCrashed = 1,
    kLandingEjected = 2,
    kLandingSafe = 3,
    kMissionEndEvent = 8,
    kFrameSyncMin = 1,
    kFrameSyncMax = 4,
    kFrameSyncDividend = 120,
    kFrameSyncBias = 9,
    kBulletTrackMin = 3,
    kBulletTrackMax = 16,
    kThreatTimerScale = 250,
    kThreatDisplayScale = 200,
    kLodRecordCount = 6,
    kLodDistanceBase = 0x0800,
    kLodDistanceScale = 0x1800,
    kLodFarMin = 0x1000,
    kLodFarMax = 9999,
    kLodDetailCap = 2,
    kLodBaseDistance = 0x20,
    kLodSummaryStep = 0x0D05,
    kVoiceGunCue = 0,
    kGunAmmoFull = 1000,
    kFuelFull = 10000,
    kEventTimerStores = 18,
    kEventTimerFuel = 12,
    kWreckTerminalFallVelocity = -16,
    kWreckGravityStep = 12,
    kSpawnSlot = 5,
    kSpawnCarrierSlot = 6,
    kSpawnTargetIdx = 3,
    kSpawnCarrierTargetIdx = 4,
    kSpawnAircraftSpec = 0,
    kSpawnMapX = 100,
    kSpawnMapY = 200,
    kSpawnNorthSouthSign = 1,
    kSpawnCarrierNorthSouthSign = -1,
    kSpawnNormalYOffset = 30,
    kSpawnCarrierXOffset = 3,
    kSpawnCarrierYOffset = 12,
    kSpawnNormalAltitude = 12,
    kSpawnCarrierAltitude = 140,
    kSpawnNormalSpeed = 10,
    kSpawnCarrierSpeed = 100,
    kSpawnWorldShift = 5,
    kSpawnFlags = 0x403,
    kCarrierTargetFlag = 0x200,
    kSpawnTerrainColor = 9,
    kSpawnGroundUnitCount = 10,
    kSpawnHeadingSouth = 0x8000,
    kSpawnCarrierHeadingHighByte = 0x7c,
    kExpectedOneCall = 1,
    kDestroyedAircraftSlot = 7,
    kDestroyedAircraftSpec = 2,
    kDestroyedAircraftX = 123,
    kDestroyedAircraftY = 234,
    kDestroyedAircraftAlt = 345,
    kDestroyedAircraftFlags = 0x4800,
    kDestroyedStoppedFlags = 0,
    kDestroyedEventType = 3,
    kDestroyedEventArg = 0x82,
    kDestroyedRepeatEventArg = 0x02,
    kDestroyedWreckFallVelocity = 0x80,
    kDestroyedSoundId = 2,
    kMissileWeaponType = 1,
    kBombPanelId = 0x16,
    kBombMissionStatus = 2,
    kBombExpectedHits = 3,
    kBombDamageBitMask = 0xff,
    kBombSuppressionFlag = 0x1000,
    kFireSidewinderLoadout = 0,
    kFireAmraamLoadout = 1,
    kFireGunLoadout = 2,
    kFireSidewinderWeapon = 0,
    kFireAmraamWeapon = 1,
    kFireHarpoonWeapon = 4,
    kFireMaverickWeapon = 5,
    kFireRetardedBombWeapon = 7,
    kFireFreefallBombWeapon = 9,
    kFireGunWeapon = 18,
    kPlayerMissileFirstSlot = 8,
    kPlayerMissileLastSlot = 11,
    kFireChosenSlot = 11,
    kFireBusyTtl = 5,
    kFireAmmoEmpty = 0,
    kFireAmmoStart = 2,
    kFireAmmoAfterShot = 1,
    kFireViewX = 1111,
    kFireViewY = 2222,
    kFireViewZ = 3333,
    kFireAltitudeBias = 20,
    kFireVelocity = 0x6000,
    kFireSpeedShift = 11,
    kFireHeading = 0x1234,
    kFirePitch = 0x0800,
    kFireRoll = 0x0100,
    kFirePitchBias = 0x1000,
    kFireLandingClear = 0,
    kFireRollAbort = 0x3001,
    kFirePanelMode = 0x15,
    kFirePanelId = 0x15,
    kFireSoundWithLock = 18,
    kFireSoundNoLock = 24,
    kFireDirectorMode = 1,
    kFireDirectorDelay = 3,
    kFireEventPriority = 1,
    kFireFrameTick = 100,
    kFireFrameRateScaling = 20,
    kFireTtlShiftBase = 6,
    kFireTtlGroundWeaponClass = 6,
    kFireTtlGroundShift = 3,
    kFireTtlDefaultShift = 2,
    kFireTtlSpeedShift = 6,
    kFireTtlSpeedBias = 1,
    kFireTtlBias = 6,
    kFireFallbackTtl = 999,
    kFireGroundLockIdx = 4,
    kFireLoftTargetRef = -0x0400,
    kFireRetardedBombPitch = 0xC000,
    kFireRetardedBombSpeed = 1,
    kCountermeasureFlare = 1,
    kCountermeasureChaff = 2,
    kCountermeasureTtlMissionStatus = 2,
    kCountermeasureFrameRate = 20,
    kCountermeasureExpectedTtl = 180,
    kBulletInitialPosX = 100,
    kBulletInitialPosY = 200,
    kBulletInitialAlt = 300,
    kBulletVelX = 4,
    kBulletVelY = -5,
    kBulletVelZ = 6,
    kGunFireFrameTick = 3,
    kGunFireFrameRate = 20,
    kGunFireAmmoStart = 1000,
    kGunFireAmmoAfterShot = 998,
    kGunFireSound = 4,
    kTracerSmokeSource = 3,
    kTracerFrameTick = 0x20,
    kTracerSpawnSlot = 2,
    kTracerAltReset = 0x80,
    kParticleSpinHighByteStep = 6,
    kFrameRandomMask = 0x7ff8,
    kFrameRandomSeedNoiseA = 0x0007,
    kFrameRandomSeedNoiseB = 0x0009,
    kFrameRandomUnusedVal = 0x0000,
    kFrameRandomNightSeed = 2,
    kWaypointFeatureShapeId = 5,
    kWaypointFeatureDynamicNameIndex = 4,
    kWaypointFeatureShapeOffset = 0x4321,
    kTargetDynamicNameFlag = 0x0100,
    kKeyCtrlY = 0x1500,
    kKeyRadarRange = 0x1372,
    kKeyZoomIn = 0x2c7a,
    kKeyZoomOut = 0x2d78,
    kKeyFlare = 0x2166,
    kKeyChaff = 0x2e63,
    kKeyLandingGear = 0x266c,
    kKeyDetailLevel = 0x2000,
    kKeySensitivity = 0x2500,
    kKeyMemoryAvailable = 0x3200,
    kKeyJiffiesFrame = 0x2100,
    kKeySlowMotion = 0x1e00,
    kKeySoundLevel = 0x2f00,
    kKeyNightMode = 0x3100,
    kKeyTacticalMap = 0x1400,
    kKeySidewinder = 0x1f73,
    kKeyAmraam = 0x326d,
    kKeyMaverick = 0x2267,
    kKeyDirector = 0x2064,
    kKeyWaypoint = 0x1177,
    kKeyAutopilot = 0x1970,
    kKeyGroundTargetLock = 0x1474,
    kKeyBackspaceFire = 0x0e08,
    kKeyEnterFire = 0x1c0d,
    kKeySpaceForwardView = 0x3920,
    kKeyFunction1 = 0x3b00,
    kKeyFunction2 = 0x3c00,
    kKeyFunction3 = 0x3d00,
    kKeyFunction4 = 0x3e00,
    kKeyFunction5 = 0x3f00,
    kKeyFunction6 = 0x4000,
    kKeyFunction7 = 0x4100,
    kKeyFunction8 = 0x4200,
    kKeyFunction9 = 0x4300,
    kKeyFunction10 = 0x4400,
    kKeyEscapeEject = 0x011b,
    kKeyDebugReloadWeapons = 0x1300,
    kKeyMapPanDown = 0x1f00,
    kKeyMapPanUp = 0x2d00,
    kKeyMapPanLeft = 0x2c00,
    kKeyMapPanRight = 0x2e00,
    kTextBlinkBiosInterrupt = 0x10,
    kTextBlinkBiosFunction = 0,
    kTextBlinkDisableMode = 0x8d,
    kRadarLongRange = 0,
    kRadarMediumRange = 1,
    kRadarShortRange = 2,
    kDetailModecodeCga = 3,
    kDetailWrappedValue = 3,
    kSensitivityWrappedValue = 0,
    kAutopilotMinimumAlt = 1000,
    kFunction1KeyValue = 0x44,
    kFunction2KeyValue = 0x42,
    kFunction3KeyValue = 0x43,
    kFunction4KeyValue = 0x41,
    kFunction5KeyValue = 0x87,
    kFunction6KeyValue = 0x84,
    kFunction7KeyValue = 0x85,
    kFunction8KeyValue = 0x89,
    kFunction9KeyValue = 0x88,
    kFunction10KeyValue = 0x8b,
    kEjectCameraKeyValue = 0x8c,
    kLandingGearSound = 32,
    kLandingGearPriority = 2,
    kEjectFirstSound = 2,
    kEjectSecondSound = 34,
    kEjectBailoutSurvived = 2,
    kEjectCrashOutcome = 6,
    kTacticalMapFlag = 0x1000,
    kCommSetupTacticalFlagOffset = 0x30,
    kMapPanStep = 0x20000,
    kMapPanZoom = 1,
    kFireCooldownStart = 3,
    kFireCooldownAfterDispatch = 2,
    kSlowMotionExitScaleStart = 8,
    kSlowMotionExitScaleAfter = 15,
    kWeaponAmmoHudY = 190,
    kWeaponAmmoClearY2 = 194,
    kWeaponAmmoColor = 0x0c,
    kWeaponAmmoClearColor = 0,
    kTargetBoxCenterX = 160,
    kTargetBoxCenterY = 56,
    kTargetBoxSize = 8,
    kTargetBoxHalfHeight = 6,
    kTargetBoxHalfScaleSize = 4,
    kTargetBoxHalfScaleHalfHeight = 3,
    kTargetLabelColor = 11,
    kTargetLabelSize = 6,
    kTargetLabelTextX = 160 - 4 * 2,
    kTargetLabelTextY = 56 + 5,
    kMissileLockCrossX = 268,
    kMissileLockCrossY = 156,
    kMissileLockColor = 14,
    kOverlayProjectileX = 150,
    kOverlayProjectileY = 60,
    kOverlayProjectileDepth = -16,
    kOverlayProjectileBoxSize = 6,
    kOverlayProjectileBoxHalfHeight = 5,
    kOverlayGroundTargetIdx = 0,
    kOverlayGroundTargetMapX = 0x2100,
    kOverlayGroundTargetMapY = 0x3100,
    kOverlayGroundTargetRangeRaw = 12 << kRangeKmShift,
    kOverlayGroundTargetBoxSize = 8,
    kOverlayPanelModeTarget = 0x13,
    kOverlayWeaponGround = 0,
    kOverlayWeaponAir = 1,
    kOverlayWeaponGroundGuided = 2,
    kOverlayAirTargetIdx = 0,
    kOverlayAirTargetSpec = 0,
    kOverlayNoModelAirTargetSpec = 8,
    kOverlayAirTargetX = 0x2200,
    kOverlayAirTargetY = 0x3200,
    kOverlayAirTargetAlt = 0x120,
    kOverlayAirTargetSpeed = 100,
    kOverlayAirTargetPitch = 0,
    kOverlayAirTargetBank = 0,
    kOverlayScopeTimer = 4,
    kOverlayScopeFrameRate = 20,
    kOverlayScopeLabelSize = kOverlayScopeFrameRate - kOverlayScopeTimer,
    kOverlayAirLockDepth = -64,
    kOverlayGroundLockDepth = -64,
    kOverlayGuidedLockX = 160,
    kOverlayGuidedLockY = 56,
    kOverlayWeaponPaveway = 6,
    kOverlayWeaponRockeye = 7,
    kOverlayWeaponSlick = 9,
    kOverlayRetardedBombSpecIndex = 29,
    kOverlayFreefallBombSpecIndex = 30,
    kOverlayHitEffectDepth = -16,
    kOverlayGroundHitAlt = -1,
    kOverlayEffectLineCount = 8,
    kOverlayExpectedReticleLines = 7,
    kOverlayFreefallGroundReticleLines = 13,
    kOverlayHudWhite = 0x0f,
    kOverlayLoftHudY = 96,
    kOverlayBombRangeRaw = 16 << kRangeKmShift,
    kOverlayInvalidMapCellFlag = 1,
    kOverlayOffBoresightBearing = 0x3000,
    kAamPanelMode = 0x13,
    kAamCandidateIdx = 1,
    kAamCandidateRange = 100,
    kAamCooldownStart = 2,
    kAamCooldownAfterTick = 1,
    kAamNoCandidateCooldown = 4,
    kAamDropLockedIdx = 0x81,
    kAamOffBoresightBearing = 0x3000,
    kUpdateVisualDepthNear = -8,
    kUpdateVisualDepthFar = -40,
    kUpdateVisualParticleX = 4,
    kUpdateVisualParticleY = 5,
    kUpdateVisualParticleAlt = 6,
    kUpdateVisualParticleSpin = 0x100,
    kUpdateVisualGroundRange = 100,
    kUpdateVisualGroundX = 7,
    kUpdateVisualGroundY = 8,
    kUpdateVisualGroundAlt = 9,
    kUpdateVisualFarDotDepth = -64,
    kUpdateVisualLockedObj = 0x80,
    kUpdateVisualFarRange = 4800,
    kUpdateVisualPositiveDepth = 1,
    kUpdateVisualGroundWorldX = kUpdateVisualGroundX << kSpawnWorldShift,
    kUpdateVisualGroundWorldY = kUpdateVisualGroundY << kSpawnWorldShift,
    kUpdateVisualProjectileX = 10,
    kUpdateVisualProjectileY = 11,
    kUpdateVisualProjectileAlt = 12,
    kUpdateVisualWreckX = 13,
    kUpdateVisualWreckY = 14,
    kUpdateVisualWreckAlt = 15,
    kUpdateVisualPlayerKey = 0x81,
    kThreatAlertTimerInit = 42,
    kThreatAlertMapX = 0x1234,
    kThreatAlertMapY = 0x2345,
    kThreatAlertViewX = 0x3456,
    kThreatAlertViewY = 0x4567,
    kThreatAlertViewZ = 0x5678,
    kThreatAlertHead = 0x6789,
    kThreatAlertMinAlert = 32,
    kThreatSiteActiveType = 1,
    kThreatSiteInitialTimer = 1,
    kThreatSiteResetTimer = 7,
    kThreatFireToneLow = 4,
    kThreatFireToneMedium = 12,
    kThreatFireToneHigh = 14,
    kThreatFireSa10Type = 4,
    kThreatFireRangeX = 0x1000,
    kThreatFireFarRangeX = 0x7000,
    kThreatFireScopeBias = 1,
    kThreatFireInitialAlert = 240,
    kThreatFireCappedAlert = 255,
    kThreatFireSound = 6,
    kThreatFireSoundPriority = 2,
    kThreatFireEventPriority = 2,
    kThreatFireDirectorKey = 0x89,
    kThreatScopeArcStartOffset = 0x20,
    kThreatSiteFireTimer = 5,
    kThreatSiteFireFlag = 0x02,
    kThreatSiteAttackFlag = 0x10,
    kThreatSiteScopeRange = 4,
    kThreatSiteFrameRate = 60,
    kThreatSiteSweepTimer = 30,
    kThreatSiteArcRadius =
        ((kThreatSiteFrameRate - kThreatSiteSweepTimer) * kThreatSiteScopeRange /
         kThreatSiteFrameRate) << 6,
    kThreatSiteLowDetailRadius = kThreatSiteScopeRange << 6,
    kThreatObjectBulletSlot = 0,
    kThreatSpawnObjIdx = 1,
    kThreatSpawnMissionTick = 16,
    kThreatSpawnMissionStatus = 20,
    kThreatSpawnDirectorObj = 0x20 + kThreatSpawnObjIdx,
    kThreatMoveObjIdx = 0,
    kThreatMoveFrameTick = 1,
    kThreatModeThreeTick = 1,
    kThreatMoveSpeed = 1,
    kThreatHighSpeed = 800,
    kThreatHighAlt = 400,
    kThreatDivePitch = -0x1000,
    kThreatMoveAlt = 1000,
    kThreatMoveTimer = 2,
    kThreatPadlockObjIdx = 0,
    kThreatPadlockTargetIdx = 1,
    kThreatPadlockFlags = 0x0103,
    kThreatLandingFlags = 0x0207,
    kThreatLandedFlag = 0x1000,
    kThreatCrashedFlag = 0x20,
    kThreatCrashEffectTimer = -8,
    kThreatModeTwoMovingFlags = 1 | 2 | 4,
    kThreatLandedMovingFlags = kThreatLandedFlag | kThreatModeTwoMovingFlags,
    kThreatTimerRetargetObjIdx = 0,
    kThreatTimerRetargetPlaneIdx = 3,
    kThreatFarDeactivateObjIdx = 1,
    kThreatFarDeactivateMissionTick = 248,
    kThreatFarDeactivatePos = 30000,
    kThreatFarDeactivateTimerAfterUpdate = -1,
    kThreatGunTrackSlot = 4,
    kThreatMapEventTtl = 1,
    kThreatPadlockMapEventX = 0x2300,
    kThreatPadlockMapEventY = 0x3100,
    kThreatManeuverObjIdx = 1,
    kThreatManeuverCloseRange = 0x200,
    kThreatManeuverModeOneRange = 0x800,
    kThreatManeuverAggressiveStatus = 1,
    kThreatManeuverHighBank = 0x5000,
    kThreatManeuverRelIndex2 = 2,
    kThreatManeuverRelIndex3 = 3,
    kThreatPlayerCarrierFlag = 0x400,
    kThreatForcedClimbFlag = 0x2000,
    kThreatVisibilityFlag = 0x20,
    kThreatVisibilityByte = 1,
    kThreatNearGroundAlt = 10,
    kThreatExtremePitch = 0x5000,
    kThreatHighClampAlt = 30001,
    kThreatRunwayOffset = 0x500,
    kThreatRunwayTouchdownFlag = 0x200,
    kThreatRolloutFlags = 0x406,
    kThreatLandingClearGroundUnits = 5,
    kThreatLandingRolloutObjIdx = 1,
    kThreatTargetCompleteState = 5,
    kThreatNormalApproachFlags = 0x0007,
    kThreatCarrierTakeoffFlags = 0x0407,
    kThreatIndicatorSafeColor = 8,
    kVisibleWorldX = 123,
    kVisibleWorldY = 456,
    kVisibleWorldZ = 789,
    kWorldCoordShift = 5,
    kOriginalMapYOffset = 0x8000,
    kGroundTargetIdx = 3,
    kGroundTargetMapX = 11,
    kGroundTargetMapY = 654,
    kGroundTargetNameIdx = 2,
    kGroundTargetShape = 0x104,
    kGroundTargetShapeTag = 0x04,
    kGroundTargetShapeOffset = 0x2468,
    kGroundTargetFlagDestroyed = 0x80,
    kGroundTargetFlagPrimary = 0x1000,
    kGroundTargetEventPrimary = 0x81,
    kGroundTargetEventInactive = 12,
    kGroundTargetEventArg = 3,
    kGroundTargetWeaponType = 2,
    kAirThreatObjIdx = 2,
    kAirThreatSpec = 0,
    kAirThreatInitialDamage = 0,
    kAirThreatDifficulty = 1,
    kAirThreatMissionStatus = 0,
    kAirThreatDamageStep = 0x30,
    kAirThreatHeatedFlag = 0x08,
    kAirThreatDecayDamage = 0x10,
    kAirThreatDecayRangeX = 0x7000,
    kAirThreatLaunchedFlag = 0x40,
    kAirThreatLaunchSpec = 5,
    kAirThreatLaunchWeapon = 18,
    kAirThreatLaunchMissionStatus = 2,
    kAirThreatLaunchDamage = 0xC0,
    kAirThreatLaunchRangeX = 0x0800,
    kAirThreatLaunchHeadingBias = 0x0400,
    kAirThreatLaunchPitch = 0x0800,
    kAirThreatLaunchBank = 0x0100,
    kAirThreatLaunchSlot = kAirThreatObjIdx % (kAirThreatLaunchMissionStatus + 1),
    kAirThreatDirectorObj = 0x20 + kAirThreatObjIdx,
    kAirThreatProjectileAltBias = 25,
    kAirThreatProjectilePitchBias = 0x0400,
    kThreatTargetingSlot = 0,
    kThreatTargetingSpec = 18,
    kThreatTargetingMode3Spec = 4,
    kThreatTargetingStartX = 0x2000,
    kThreatTargetingStartY = 0x3000,
    kThreatTargetingTargetX = 0x2064,
    kThreatTargetingTargetY = 0x3000,
    kThreatTargetingStartAlt = 1000,
    kThreatTargetingTtl = 10,
    kThreatTargetingSpeed = 100,
    kThreatTargetingSound = 10,
    kThreatTargetingIndicator = 0,
    kThreatTargetingIndicatorColor = 0x0E,
    kThreatTargetingPlayerSlot = 8,
    kThreatTargetingA2aSpec = 22,
    kThreatTargetingGroundSpec = 27,
    kThreatTargetingGuidedBombSpec = 28,
    kThreatTargetingFreefallSpec = 30,
    kThreatTargetingFreefallWeapon = 9,
    kThreatTargetingModeZeroSpec = 7,
    kThreatTargetingGuidanceDecayPitch = 0x1000,
    kThreatTargetingGuidanceDecayFrameRate = 16,
    kThreatTargetingGuidanceDecayStep = 0x0100,
    kThreatTargetingMode28PitchClamp = -0x0800,
    kThreatTargetingMode30TargetRef = -0x0100,
    kThreatTargetingPlayerSpeed = 200,
    kThreatTargetingCloseRange = 4,
    kThreatTargetingStaleRefRange = 0x300,
    kThreatTargetingFreefallMissOffset = 0x400,
    kThreatTargetingFarWaypointOffset = 0x800,
    kThreatTargetingGroundTargetIdx = 0,
    kThreatTargetingGroundTargetLock = -1,
    kThreatTargetingInvalidPlaneRef = 3,
    kThreatTargetingInvalidSimRef = 0,
    kThreatTargetingExplosionTimer = 8,
    kThreatTargetingMissTimer = -3,
    kThreatTargetingDirectHitTtl = 5,
    kThreatTargetingPlayerHitEvent = 5,
    kThreatTargetingLongHitTtl = kFireFrameRateScaling * 10 + 2,
    kThreatTargetingPlaneZeroTileId = 3,
    kThreatTargetingLandSymbol = 1,
    kThreatTargetingWaterSymbol = 2,
    kThreatTargetingCountermeasureEventSlot = 1,
    kThreatTargetingCountermeasureTtl = 3,
    kThreatTargetingHeatseekerSpec = 17,
    kThreatTargetingImpactSpec = 24,
    kThreatTargetingImpactWeapon = 2,
    kThreatTargetingPlaneDirectorBase = 0x40,
    kSamAcquireSlot = 0,
    kSamAcquireMapX = 0,
    kSamAcquireMapY = 0,
    kSamAcquireTargetRange = 1024,
    kSamAcquireSlowSpeed = 1,
    kSamAcquireTtlLarge = 500,
    kSamAcquireClampedTtl = kFireFrameRateScaling << 4,
    kSamAcquireModeRadar = 0,
    kSamAcquireModeHeat = 1,
    kSamAcquireHeadingNorth = 0,
    kSamAcquireHeadingEast = 0x4000,
    kSamAcquireHeadingRear = 0x7000,
    kTargetReachedPrimaryEvent = 0x8b,
    kTargetReachedSecondaryEvent = 0x4b,
    kRadioAutopilotAltitude = 500,
    kRadioDirectorMode = 2,
    kRadioViewTargetPlaneBase = 0x40,
    kRadioViewTargetGroundBase = 0x20,
    kRadioDirectorKey = 0x89,
    kRadioStrikeEagleKey = 0x87,
    kRadioPlaneCount = 6,
    kRadioGroundUnitCount = 4,
    kRadioAircraftIdxFromSeed = 5,
    kRadioGroundIdxFromSeed = 1,
    kRadioAircraftNameIdx = 2,
    kRadioPendingDeadline = 123,
    kMissionStringPlaneIndex = 2,
    kMissionStringPlaneX = 111,
    kMissionStringPlaneY = 222,
    kMissionStringWaypointX = 333,
    kMissionStringWaypointY = 444,
    kMissionStringDifficultyCombat = 1,
    kMissionStringDifficultyTraining = 0,
    kMissionWaypointCount = 4,
    kMissionTargetSlotCount = 2,
    kMissionTargetNameCount = 100,
    kMissionStringPoolSize = 0x2EE,
    kUpdateFrameInitPhaseSteady = 2,
    kUpdateFrameScale = 20,
    kUpdateFrameTick = 1,
    kUpdateFrameViewX = 0x2000,
    kUpdateFrameViewY = 0x3000,
    kUpdateFrameWorldX = kUpdateFrameViewX << kWorldCoordShift,
    kUpdateFrameWorldY = (kOriginalMapYOffset - kUpdateFrameViewY) << kWorldCoordShift,
    kUpdateFrameViewZ = 1000,
    kUpdateFrameBulletTrackCount = 4,
    kUpdateFrameNoThreatRange = 0x7fff,
    kUpdateFrameExpectedTick = kUpdateFrameTick + 1,
    kUpdateFrameScreenXOutsideScope = 20,
    kUpdateFrameScreenYOutsideScope = 100,
    kUpdateFrameMapZoomStart = 3,
    kUpdateFrameMapZoomAfterMarker = 2,
    kUpdateFrameDirectorKey = 0x87,
    kUpdateFrameVoiceCueDestroyed = 2,
    kUpdateFrameVoiceCueEngineStart = 0,
    kUpdateFrameAlertPlaneIdx = 3,
    kUpdateFrameAlertThreshold = 0xc0,
    kUpdateFrameAlertTimingAccum = 75,
    kUpdateFrameCrashKnots = 60,
    kUpdateFrameCrashGroundAlt = 1,
    kUpdateFrameTrainingKey = 0x69,
    kUpdateFrameInitDifficultyCombat = 1,
    kUpdateFrameInitDifficultyAutopilot = 4,
    kUpdateFrameInitDifficultyAfterAutopilot = 2,
    kUpdateFrameInitTheater = 0,
    kUpdateFrameInitCarrierTheater = 6,
    kUpdateFrameCarrierStartFlag = 0x200,
    kUpdateFrameCarrierStartOffset = 0x80,
    kUpdateFrameCarrierStartBrakeFlag = 0x08,
    kUpdateFrameAutopilotPlayerFlag = 0x1000,
    kUpdateFrameWingmanSlot = 1,
    kUpdateFrameLandingTargetX = 0x2100,
    kUpdateFrameLandingTargetY = 0x3100,
    kUpdateFrameLandingFlags = 0x500,
    kUpdateFrameCarrierLandingFlags = kUpdateFrameLandingFlags | kUpdateFrameCarrierStartFlag,
    kUpdateFrameLandingTimerStart = 1,
    kUpdateFrameLandingTimerAfterSafe = 2,
    kUpdateFrameLandingTimerAfterResupply = kUpdateFrameScale + 2,
    kUpdateFrameLandingMessageTimer = kUpdateFrameScale + 1,
    kUpdateFrameLandingWeaponsTick = 0,
    kUpdateFrameLandingReadyTick = 8,
    kUpdateFrameSafeLandingVoiceCue = 4,
    kUpdateFrameAutoLandingFlags = 0x6000,
    kUpdateFrameAutoLandingScale = 7,
    kUpdateFrameAutoLandingSpeed = 5400,
    kUpdateFrameAutoLandingTick = 1,
    kUpdateFrameAutoLandingOffset = 0x40,
    kUpdateFrameCarrierDeckApproachOffset = 0x10,
    kUpdateFrameCarrierDeckCrashKnots = 0x51,
    kUpdateFrameCarrierDeckCrashSound = 22,
    kUpdateFrameAutoLandingStepDivisor = 14,
    kUpdateFrameAutoLandingExpectedAlt =
        kUpdateFrameViewZ - (kUpdateFrameViewZ / kUpdateFrameAutoLandingStepDivisor),
    kUpdateFrameCarrierDeckAlt = 0x80,
    kUpdateFrameAutoLandingCarrierAlt = kUpdateFrameCarrierDeckAlt + 1,
    kUpdateFrameCarrierMinAutoLandingAlt = kUpdateFrameCarrierDeckAlt + 5,
    kUpdateFrameAutoLandingWorldStep =
        (kUpdateFrameAutoLandingOffset << kWorldCoordShift) / kUpdateFrameAutoLandingStepDivisor,
    kUpdateFrameMinViewX = 0x0100,
    kUpdateFrameMaxViewY = 0x7d00,
    kUpdateFrameThreatTimerStart = 2,
    kUpdateFrameCarrierThreatIdx = 1,
    kUpdateFrameCarrierFlags = 0x701,
    kUpdateFrameCarrierX = 0x2200,
    kUpdateFrameCarrierY = 0x3200,
    kUpdateFrameCarrierSpec = 18,
    kUpdateFrameCarrierEscortSpec = 9,
    kUpdateFrameCarrierAlt = 132,
    kUpdateFrameCarrierOffset = 5,
    kUpdateFrameNonCarrierFlags = 0x501,
    kUpdateFrameNonCarrierX = 0x2300,
    kUpdateFrameNonCarrierY = 0x3300,
    kUpdateFrameNonCarrierOffsetX = 10,
    kUpdateFrameNonCarrierOffsetY = 0x10,
    kUpdateFrameNonCarrierAlt = 4,
    kUpdateFrameGroundUnitsForThreat = 8,
    kUpdateFrameAlternateAttackFlags = 0x800,
    kUpdateFrameAlternateAttackRangeY = 0x400,
    kUpdateFrameSavedAltitude = 100,
    kUpdateFrameRecoveredAltitude = 600,
    kUpdateFrameMissionEventTick = 0x1f,
    kUpdateFrameMissionEventType = 9,
    kUpdateFrameRadioMissionTick = 3,
    kUpdateFrameRadioMissionTickAfterUpdate = 4,
    kUpdateFrameGroundAlertDamage = 0xc1,
};

int g_lastAudioSound = -1;
int g_audioSoundCalls = 0;
int g_lastAudioSample = -1;
int g_audioSampleCalls = 0;
int g_engineOnCalls = 0;
int g_engineOffCalls = 0;
char g_lastHudMessage[64] = {};
char g_lastTimedMessage[64] = {};
int g_lineDrawCalls = 0;
int g_hudLineCalls = 0;
int g_lastDrawColor = -1;
int g_stringActiveCalls = 0;
char g_lastActiveString[64] = {};
int g_lastActiveStringX = 0;
int g_lastActiveStringY = 0;
int g_lastActiveStringColor = 0;
struct LineCall {
    int x1;
    int y1;
    int x2;
    int y2;
};
LineCall g_fullscreenLines[16] = {};
LineCall g_hudLines[32] = {};
int g_fuelGaugeCalls = 0;
int g_throttleStateCalls = 0;
int g_readMapPixelCalls = 0;
int g_lastReadMapX = 0;
int g_lastReadMapY = 0;
int g_refreshPanelCalls = 0;
int g_lastRefreshPanel = -1;
int g_timedMessageCalls = 0;
int g_clearStatusPanelCalls = 0;
int g_setupDacCalls = 0;
int g_addTileEntryCalls = 0;
int g_switchIndicatorCalls = 0;
int g_zoomInCalls = 0;
int g_zoomOutCalls = 0;
int g_restoreScopeCalls = 0;
int g_captureScopeCalls = 0;
int g_drawMapRangeArcCalls = 0;
int g_lastMapArcX = 0;
int g_lastMapArcY = 0;
int g_lastMapArcRadius = 0;
int g_lastMapArcColor = 0;
int g_lastMapArcRadarFlag = 0;
int g_lastMapArcStart = 0;
int g_lastMapArcEnd = 0;
int g_blitSpriteCalls = 0;
int g_fillPanelBoxCalls = 0;
int g_projectSceneObjectCalls = 0;
int g_computeMapTargetRangeResult = 0;
int g_computeSimObjectRangeResult = 0;
int g_computeTargetBearingResult = 0;
int g_computeTargetBearingValue = 0;
int g_missileTargetCompatResult = 0;
int g_findWaypointEntryResult = -1;
int g_isTargetOverWaterResult = 0;
int g_lastIndicatorIdx = -1;
int g_lastIndicatorColor = -1;
int g_lastTileEntryValue = 0;
int g_lastTileEntryShape = 0;
int g_stubTargetSymbol = 0;
uint32 g_lastNearestWorldX = 0;
uint32 g_lastNearestWorldY = 0;
uint32 g_lastDrawNearestCoord1 = 0;
uint32 g_lastDrawNearestCoord2 = 0;
uint32 g_lastDrawNearestCoord3 = 0;
int g_drawNearestTileObjectCalls = 0;
int g_drawNearestVisibilityByte = 0;
int g_gfxFlipCalls = 0;
int g_gfxCopyRectCalls = 0;
int g_redrawTacMapCalls = 0;
int g_objectToScreenResult = 0;
int16 g_objectToScreenX = 0;
int16 g_objectToScreenY = 0;
int g_lastRedrawX = 0;
int g_lastRedrawY = 0;
struct TileObject g_stubNearestTile = {};

struct HudProjection {
    bool visible;
    int x;
    int y;
    int depth;
};
HudProjection g_hudProjectionScript[16] = {};
int g_hudProjectionCount = 0;
int g_hudProjectionIdx = 0;

struct RectRecord {
    int x1;
    int y1;
    int x2;
    int y2;
};

struct NumberRecord {
    int value;
    int x;
    int y;
    int color;
};

RectRecord g_rectRecords[12] = {};
int g_rectRecordCount = 0;
NumberRecord g_numberRecords[12] = {};
int g_numberRecordCount = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void resetGameplayState() {
    std::memset(g_planeTable.planes, 0, sizeof(g_planeTable.planes));
    std::memset(g_projectiles, 0, sizeof(struct Projectile) * 12);
    std::memset(bulletTracks, 0, sizeof(struct BulletTrack) * 20);
    std::memset(g_targetSlots, 0, sizeof(struct TargetSlot) * 2);
    std::memset(mapEvents, 0, sizeof(struct MapEvent) * 10);
    std::memset(g_mapCellFlags, 0, 0x100);
    std::memset(g_tileKillTally, 0, 0x100);
    strBuf[0] = '\0';
    g_viewX_ = 0;
    g_viewY_ = 0;
    g_viewZ = 0;
    g_ourHead = 0;
    g_frameRateScaling = 60;
    g_missionStatus = 0;
    g_targetEntityCount = 0;
    g_playerPlaneFlags = 0;
    waypointIndex = 0;
    g_acqRange = 0;
    g_acqAimY = 0;
    g_eventLogCount = 0;
    std::memset(&g_replayLog, 0, sizeof(g_replayLog));
    g_directorMode = 0;
    g_directorEventDeadline = -1;
    keyValue = 0;
    g_viewTargetObj = 0;
    g_autopilotAltitude = 0;
    keyScancode = 0;
    frameTick = 0;
    g_ejectState = 0;
    g_inputDisabled = 0;
    g_fireCooldown = 0;
    g_missionEndedFlag[0] = 0;
    g_bombDamageMask = 0;
    g_damageTakenFlag = 0;
    g_gunHits = 0;
    g_finalThreatScore = 0;
    g_resupplyCount = 0;
    g_missionTick = 0;
    g_lastSpawnTick = 0;
    g_initPhase = 0;
    g_frameRateAccum = 0;
    g_frameTimingAccum = 0;
    g_jiffiesPerFrame = 0;
    g_enemyAlertFlag = 0;
    g_slowMotionMode = 0;
    g_frameSyncWait = 0;
    g_bulletTrackCount = 0;
    g_threatTimerInit = 0;
    g_threatActiveTimer = 0;
    g_threatDisplayTtl = 0;
    g_threatRefX = 0;
    g_threatRefY = 0;
    g_threatRefZ = 0;
    g_threatRefHead = 0;
    g_padlockAircraft = -1;
    g_activeThreatCount = 0;
    g_enemyThreatCount = 0;
    g_nearestThreatRange = 0;
    g_closestThreatIndex = 0;
    g_detailLevel = 0;
    g_lodDistBase = 0x800;
    g_lodDistScale = 0x1000;
    g_lodDistNear = 0;
    g_lodDistFar = 0;
    g_hudVisible = 0;
    g_halfScaleRender = 0;
    g_weaponMarkerSel = 0;
    missileSpecIndex = 0;
    g_currentWeaponType = 0;
    g_activePanelMode = 0;
    g_inLandingCorridor = 1;
    g_velocity = 0;
    g_setThrust = 0;
    g_ourPitch = 0;
    g_ourRoll = 0;
    g_lastMissileSlot = 0;
    g_groundTargetLock = -1;
    g_airTargetLock = -1;
    g_aamLockCooldown = 0;
    g_aamLockActive = 0;
    g_lockedTargetKilled = 0;
    f15DgtlResult = 0;
    std::memset(g_axisInputAccum, 0, sizeof(int16) * 4);
    g_lastAudioSound = -1;
    g_audioSoundCalls = 0;
    g_lastAudioSample = -1;
    g_audioSampleCalls = 0;
    g_engineOnCalls = 0;
    g_engineOffCalls = 0;
    g_lastHudMessage[0] = '\0';
    g_lastTimedMessage[0] = '\0';
    g_lineDrawCalls = 0;
    g_hudLineCalls = 0;
    g_lastDrawColor = -1;
    g_stringActiveCalls = 0;
    g_lastActiveString[0] = '\0';
    g_lastActiveStringX = 0;
    g_lastActiveStringY = 0;
    g_lastActiveStringColor = 0;
    std::memset(g_fullscreenLines, 0, sizeof(g_fullscreenLines));
    std::memset(g_hudLines, 0, sizeof(g_hudLines));
    g_fuelGaugeCalls = 0;
    g_throttleStateCalls = 0;
    g_readMapPixelCalls = 0;
    g_lastReadMapX = 0;
    g_lastReadMapY = 0;
    g_refreshPanelCalls = 0;
    g_lastRefreshPanel = -1;
    g_timedMessageCalls = 0;
    g_clearStatusPanelCalls = 0;
    g_setupDacCalls = 0;
    g_addTileEntryCalls = 0;
    g_switchIndicatorCalls = 0;
    g_zoomInCalls = 0;
    g_zoomOutCalls = 0;
    g_restoreScopeCalls = 0;
    g_captureScopeCalls = 0;
    g_drawMapRangeArcCalls = 0;
    g_lastMapArcX = 0;
    g_lastMapArcY = 0;
    g_lastMapArcRadius = 0;
    g_lastMapArcColor = 0;
    g_lastMapArcRadarFlag = 0;
    g_lastMapArcStart = 0;
    g_lastMapArcEnd = 0;
    g_blitSpriteCalls = 0;
    g_fillPanelBoxCalls = 0;
    g_projectSceneObjectCalls = 0;
    g_computeMapTargetRangeResult = 0;
    g_computeSimObjectRangeResult = 0;
    g_computeTargetBearingResult = 0;
    g_computeTargetBearingValue = 0;
    g_missileTargetCompatResult = 0;
    g_findWaypointEntryResult = -1;
    g_isTargetOverWaterResult = 0;
    g_lastIndicatorIdx = -1;
    g_lastIndicatorColor = -1;
    g_lastTileEntryValue = 0;
    g_lastTileEntryShape = 0;
    g_stubTargetSymbol = 0;
    g_lastNearestWorldX = 0;
    g_lastNearestWorldY = 0;
    g_lastDrawNearestCoord1 = 0;
    g_lastDrawNearestCoord2 = 0;
    g_lastDrawNearestCoord3 = 0;
    g_drawNearestTileObjectCalls = 0;
    g_drawNearestVisibilityByte = 0;
    g_gfxFlipCalls = 0;
    g_gfxCopyRectCalls = 0;
    g_redrawTacMapCalls = 0;
    g_objectToScreenResult = 0;
    g_objectToScreenX = 0;
    g_objectToScreenY = 0;
    g_lastRedrawX = 0;
    g_lastRedrawY = 0;
    g_stubNearestTile = {};
    std::memset(g_hudProjectionScript, 0, sizeof(g_hudProjectionScript));
    g_hudProjectionCount = 0;
    g_hudProjectionIdx = 0;
    std::memset(g_rectRecords, 0, sizeof(g_rectRecords));
    g_rectRecordCount = 0;
    std::memset(g_numberRecords, 0, sizeof(g_numberRecords));
    g_numberRecordCount = 0;
    g_groundUnitCount = 0;
    g_planeCount = 0;
    g_trackedEnemyIdx = 0;
    g_wreckAlt = 0;
    g_wreckFallVel = 0;
    g_hitEffectTimer = 0;
    g_hitMapX = 0;
    g_hitMapY = 0;
    g_hitAlt = 0;
    g_savedSamTtl = 0;
    g_loftTargetIdx = 0;
    g_posVisibleFlag = 0;
    g_targetInHudFlag = 0;
    g_prevKillMarker = 0;
    g_lockToneFlag = 0;
    g_unusedHudFlag = 0;
    g_aamLeadDist = 0;
    g_landingTimer = 0;
    g_landingDoneFlag = 0;
    g_autoLandingActive = 0;
    g_autoCrashDive = 0;
    g_savedPosVisible = 0;
    g_destroyedCueDeadline = 0;
    g_gearDownArmed = 0;
    g_mapZoomLevel = 0;
    g_knots = 0;
    g_nearestTileObj = &g_stubNearestTile;
    g_landTargetId[0] = 0;
    g_gunAmmo = 0;
    g_fuelRemaining = 0;
    std::memset(g_eventTimers, 0, sizeof(int16) * 8);
    std::memset(g_simObjects, 0, sizeof(struct SimObject) * 20);
}

void pushHudProjection(bool visible, int x, int y, int depth) {
    require(g_hudProjectionCount < static_cast<int>(sizeof(g_hudProjectionScript) / sizeof(g_hudProjectionScript[0])),
            "test HUD projection script capacity");
    g_hudProjectionScript[g_hudProjectionCount++] = {visible, x, y, depth};
}

int weaponIndexForSpec(int specIndex) {
    for (int idx = 0; idx < 20; ++idx) {
        if (missiles[idx].specIndex == specIndex) return idx;
    }
    require(false, "test fixture should find original weapon spec index");
    return 0;
}

int expectedThreatScoreForType(int threatType) {
    unsigned score =
        (aNone[threatType].dangerTier + g_missionStatus * 2 + 3) *
        aNone[threatType].lethality / kThreatLethalityDivisor;
    score = score * (((unsigned)g_viewZ >> kThreatAltitudeShift) + kThreatAltitudeBias) >>
            kThreatScoreShift;
    return static_cast<int>(score);
}

} // namespace

int readMapPixelColor(int mapX, int mapY) {
    ++g_readMapPixelCalls;
    g_lastReadMapX = mapX;
    g_lastReadMapY = mapY;
    return kSpawnTerrainColor;
}

void tempStrcpy(const char *src) {
    std::strncpy(g_lastHudMessage, src, sizeof(g_lastHudMessage) - 1);
    g_lastHudMessage[sizeof(g_lastHudMessage) - 1] = '\0';
}

void setTimedMessage(char *message) {
    std::strncpy(g_lastTimedMessage, message, sizeof(g_lastTimedMessage) - 1);
    g_lastTimedMessage[sizeof(g_lastTimedMessage) - 1] = '\0';
    ++g_timedMessageCalls;
}

int computeLoftAngle(void) {
    // Stubbed only for the AGM loft branch; fireMissile tests below exercise
    // Sidewinder/AMRAAM/Gun behavior and never consume this value.
    return 0;
}

void drawFullscreenLine(int x1, int y1, int x2, int y2) {
    require(g_lineDrawCalls < static_cast<int>(sizeof(g_fullscreenLines) / sizeof(g_fullscreenLines[0])),
            "test fullscreen-line recorder capacity");
    g_fullscreenLines[g_lineDrawCalls++] = {x1, y1, x2, y2};
}

void drawHudViewLine(int x1, int y1, int x2, int y2) {
    require(g_hudLineCalls < static_cast<int>(sizeof(g_hudLines) / sizeof(g_hudLines[0])),
            "test HUD-line recorder capacity");
    g_hudLines[g_hudLineCalls++] = {x1, y1, x2, y2};
}

void setDrawColor(int color) { g_lastDrawColor = color; }

void drawStringActivePage(const char *text, int screenX, int screenY, int color) {
    ++g_stringActiveCalls;
    std::strncpy(g_lastActiveString, text ? text : "", sizeof(g_lastActiveString) - 1);
    g_lastActiveString[sizeof(g_lastActiveString) - 1] = '\0';
    g_lastActiveStringX = screenX;
    g_lastActiveStringY = screenY;
    g_lastActiveStringColor = color;
}

void drawViewportLine(int x1, int y1, int x2, int y2) {
    drawHudViewLine(x1, y1, x2, y2);
}

void loadColorPalette(int) {}
void blitSprite(int, int, int, int, int, int, int) { ++g_blitSpriteCalls; }
void fillPanelBox(int, int) { ++g_fillPanelBoxCalls; }
void initTacMapView(void) {}
void setActivePanel(int) {}

int isqrt(int value) {
    int root = 0;
    while ((root + 1) * (root + 1) <= value) {
        ++root;
    }
    return root;
}

void projectWorldToHud(int, int, int) {
    if (g_hudProjectionIdx < g_hudProjectionCount) {
        const HudProjection &projection = g_hudProjectionScript[g_hudProjectionIdx++];
        vtxScratch.vproj.x.lo = projection.visible ? projection.x : -1;
        vtxScratch.vproj.y.lo = projection.visible ? projection.y : -1;
        g_projDepth = projection.depth;
        return;
    }
    vtxScratch.vproj.x.lo = -1;
    vtxScratch.vproj.y.lo = -1;
    g_projDepth = -1;
}

int computeMapTargetRange(int) {
    g_targetRange = g_computeMapTargetRangeResult;
    g_targetBearing = g_computeTargetBearingValue;
    return g_computeMapTargetRangeResult;
}

int computeSimObjectRange(int) {
    g_targetRange = g_computeSimObjectRangeResult;
    return g_computeSimObjectRangeResult;
}

int computeTargetBearing(int, int, int) {
    g_targetRange = g_computeTargetBearingResult;
    g_targetBearing = g_computeTargetBearingValue;
    return g_computeTargetBearingResult;
}
int getTargetSymbol(int) { return g_stubTargetSymbol; }
int isTargetOverWater(int) { return g_isTargetOverWaterResult; }
int missileTargetCompat(int, int) { return g_missileTargetCompatResult; }
int findWaypointEntry(int, int) { return g_findWaypointEntryResult; }
int objectToScreen(int, int, int16 *screenX, int16 *screenY) {
    if (g_objectToScreenResult != 0) {
        *screenX = g_objectToScreenX;
        *screenY = g_objectToScreenY;
    }
    return g_objectToScreenResult;
}
int FAR CDECL gfx_getDisplayPage(void) { return 0; }
void FAR CDECL gfx_flipPage(void) { ++g_gfxFlipCalls; }
void FAR CDECL gfx_waitRetrace(void) {}
void FAR CDECL gfx_copyRect(int, uint16, uint16, int, uint16, uint16, int, int) {
    ++g_gfxCopyRectCalls;
}
void waitFrameSync(int) {}
void redrawTacMap(int centerX, int centerY) {
    ++g_redrawTacMapCalls;
    g_lastRedrawX = centerX;
    g_lastRedrawY = centerY;
}
void plotMapObject(int, int, int, int) {}
void drawNearestTileObject(unsigned int coord1, unsigned int coord2, unsigned int coord3) {
    ++g_drawNearestTileObjectCalls;
    g_lastDrawNearestCoord1 = coord1;
    g_lastDrawNearestCoord2 = coord2;
    g_lastDrawNearestCoord3 = coord3;
    *(unsigned char *)&g_posVisibleFlag = static_cast<unsigned char>(g_drawNearestVisibilityByte);
}

void shiftLongLeftInPlace(int count, long *value) {
    *value <<= count;
}

void shiftLongRightInPlace(int count, long *value) {
    *value >>= count;
}

void setViewPosition(int, int, int) {}
void projectSceneObject(char *, int, int, int, int, int, int) { ++g_projectSceneObjectCalls; }
void setup3DTransform(const int16 *, int, int, int, int, int, int, int) {}
void rasterize3DWorld(void) {}
int __far fillSpanRect(const int16 *, int, int, int, int) { return 0; }
char far g_world3dData[0x100] = {};
void fillRectBoth(int x1, int y1, int x2, int y2) {
    require(g_rectRecordCount < static_cast<int>(sizeof(g_rectRecords) / sizeof(g_rectRecords[0])),
            "test fill-rect recorder capacity");
    g_rectRecords[g_rectRecordCount++] = {x1, y1, x2, y2};
}
void drawNumber(int value, int x, int y, int color) {
    require(g_numberRecordCount < static_cast<int>(sizeof(g_numberRecords) / sizeof(g_numberRecords[0])),
            "test draw-number recorder capacity");
    g_numberRecords[g_numberRecordCount++] = {value, x, y, color};
}

void refreshActivePanel(int panelId) {
    ++g_refreshPanelCalls;
    g_lastRefreshPanel = panelId;
}

void drawFuelGauge(void) { ++g_fuelGaugeCalls; }
void UpdateThrottleState(void) { ++g_throttleStateCalls; }
void clearStatusPanel(void) { ++g_clearStatusPanelCalls; }
void setupDac(void) { ++g_setupDacCalls; }
int getTimeOfDay(void) { return 1; }
int misc_readJoystick(int16) { return 0; }
int FAR CDECL gfx_getModecode(void) { return kDetailModecodeCga; }

void switchIndicatorColor(int indicatorIdx, int color) {
    ++g_switchIndicatorCalls;
    g_lastIndicatorIdx = indicatorIdx;
    g_lastIndicatorColor = color;
}

void zoomIn(void) { ++g_zoomInCalls; }
void zoomOut(void) { ++g_zoomOutCalls; }
void restoreScopePanel(void) { ++g_restoreScopeCalls; }
void captureScopePanel(void) { ++g_captureScopeCalls; }
void drawMapRangeArc(int centerX, int centerY, int radius, int color,
                     int radarFlag, int arcStart, int arcEnd) {
    ++g_drawMapRangeArcCalls;
    g_lastMapArcX = centerX;
    g_lastMapArcY = centerY;
    g_lastMapArcRadius = radius;
    g_lastMapArcColor = color;
    g_lastMapArcRadarFlag = radarFlag;
    g_lastMapArcStart = arcStart;
    g_lastMapArcEnd = arcEnd;
}

int sine(int angle) {
    switch (static_cast<uint16>(angle)) {
    case 0x0000:
    case 0x8000:
        return 0;
    case 0x4000:
        return 32767;
    case 0xC000:
        return -32767;
    default:
        return 0;
    }
}

int fixedMulQ14(int a, int b) {
    const int32 p = static_cast<int32>(a) * static_cast<int32>(b);
    const int hi15 = p >= 0 ? static_cast<int>(static_cast<uint32>(p) >> 15)
                            : -static_cast<int>((static_cast<uint32>(-p) + 0x7fffu) >> 15);
    const int hi14 = p >= 0 ? static_cast<int>(static_cast<uint32>(p) >> 14)
                            : -static_cast<int>((static_cast<uint32>(-p) + 0x3fffu) >> 14);
    return hi15 + (hi14 & 1);
}

struct TileObject *findNearestTileObject(uint32 worldX, uint32 worldY) {
    g_lastNearestWorldX = worldX;
    g_lastNearestWorldY = worldY;
    return &g_stubNearestTile;
}

void addTileEntry(struct TileObject *, int value, char shape) {
    ++g_addTileEntryCalls;
    g_lastTileEntryValue = value;
    g_lastTileEntryShape = static_cast<unsigned char>(shape);
}

int audio_playSound(int soundId) {
    g_lastAudioSound = soundId;
    ++g_audioSoundCalls;
    return 0;
}

int audio_playSample(int sampleIdx) {
    g_lastAudioSample = sampleIdx;
    ++g_audioSampleCalls;
    return 0;
}

int audio_engineDroneOn(void) {
    ++g_engineOnCalls;
    return 0;
}

int audio_engineDroneOff(void) {
    ++g_engineOffCalls;
    return 0;
}

// Copied original threat weapon fixture: names, lethality tiers, danger tiers,
// and capability flags are table data rather than independent test constants.
extern const struct Weapon aNone[] = {
    {"None", 0, 0, 0x0000},   {"SA-2", 200, 3, 0x0000},
    {"SA-5", 350, 2, 0x0000}, {"SA-8B", 125, 5, 0x0000},
    {"SA-10", 320, 7, 0x0001}, {"SA-11", 200, 5, 0x0000},
    {"SA-12", 290, 6, 0x0001}, {"SA-13", 125, 3, 0x0000},
    {"SA-N-4", 200, 4, 0x0001}, {"SA-N-5", 150, 3, 0x0000},
    {"SA-N-6", 320, 6, 0x0001}, {"SA-N-7", 200, 5, 0x0000},
    {"Hawk", 175, 6, 0x0001}, {"Rapier", 75, 8, 0x0000},
    {"Tiger", 65, 4, 0x0000}, {"Seacat", 200, 2, 0x0000},
    {"IL76", 200, 8, 0x0003}, {"", 50, 5, 0x0000},
    {"", 70, 6, 0x0000},     {"", 80, 7, 0x0001},
    {"", 100, 8, 0x0001},    {"OTH", 500, 5, 0x0001},
    {"", 40, 3, 0x0000},
};

struct GameComm *commData = nullptr;
struct Game *gameData = nullptr;

int main() {
    resetGameplayState();
    g_viewX_ = 0x2000;
    g_viewY_ = 0x3000;
    g_viewZ = 0x1000;
    g_missionStatus = 1;

    int bearing = 0;
    int range = 0;
    const int threatType = 4;
    const int score = computeThreatRangeBearing(0x1C00, 0x3400, 123, threatType, &bearing, &range);
    require(range == (rangeApprox(0x0400, -0x0400) >> kRangeKmShift),
            "computeThreatRangeBearing stores original rangeApprox distance in km units");
    require(static_cast<uint16>(bearing) == static_cast<uint16>(computeBearing(0x0400, 0x0400)),
            "computeThreatRangeBearing stores original bearing with inverted map Y");
    require(score == expectedThreatScoreForType(threatType),
            "computeThreatRangeBearing preserves original altitude-weighted threat score");
    require(computeThreatRangeBearing(0, 0, 0, 0, &bearing, &range) == 0,
            "computeThreatRangeBearing ignores empty threat type");
    require(computeThreatRangeBearing(0, 0, 0, -1, &bearing, &range) == 0,
            "computeThreatRangeBearing ignores sentinel threat type");

    resetGameplayState();
    g_missionStatus = 2;
    g_targetEntityCount = 4;
    g_planeTable.planes[0].active = 2;
    g_planeTable.planes[1].active = 0;
    g_planeTable.planes[2].active = 4;
    g_planeTable.planes[3].active = 7;
    int expectedScore = 0;
    for (int idx : {0, 2, 3}) {
        const int activeType = g_planeTable.planes[idx].active;
        expectedScore += aNone[activeType].lethality * aNone[activeType].dangerTier *
                         (g_missionStatus + 2) / kThreatScoreDivisor;
    }
    expectedScore /= kThreatTotalDivisor;
    require(computeThreatScore() == expectedScore,
            "computeThreatScore sums only active target slots with original divisors");

    resetGameplayState();
    g_projectiles[kProjectileSlot].mapX = 1000;
    g_projectiles[kProjectileSlot].mapY = 1000;
    g_projectiles[kProjectileSlot].speed = 200;
    g_projectiles[kProjectileSlot].worldX = kAngleQuarterTurn;
    g_frameRateScaling = 40;
    require(samCanAcquireTarget(kProjectileSlot, 1100, 1000, 0, 0) == 1,
            "samCanAcquireTarget succeeds when projectile can reach target this frame");
    require(g_acqRange == rangeApprox(100, 0), "samCanAcquireTarget stores acquisition range");

    resetGameplayState();
    g_projectiles[kProjectileSlot].mapX = 1000;
    g_projectiles[kProjectileSlot].mapY = 1000;
    g_projectiles[kProjectileSlot].speed = 10;
    g_projectiles[kProjectileSlot].worldX = 0;
    g_projectiles[kProjectileSlot].ttl = 1000;
    g_frameRateScaling = 20;
    require(samCanAcquireTarget(kProjectileSlot, 1000, 3000, 0, 1) == 0,
            "samCanAcquireTarget rejects targets outside the original turn cone");
    require(g_projectiles[kProjectileSlot].ttl == g_frameRateScaling << 4,
            "samCanAcquireTarget clamps far off-boresight SAM ttl for active SAM slots");

    resetGameplayState();
    g_projectiles[kProjectileSlot].mapX = 1000;
    g_projectiles[kProjectileSlot].mapY = 1000;
    g_projectiles[kProjectileSlot].speed = 10;
    g_projectiles[kProjectileSlot].worldX = 0;
    g_ourHead = kAngleHalfTurn;
    g_frameRateScaling = 20;
    require(samCanAcquireTarget(kProjectileSlot, 3000, 1000, 0, 0) == 0,
            "samCanAcquireTarget mode 0 requires original forward-heading cone");

    resetGameplayState();
    g_targetSlots[0].state = 2;
    require(markTargetReached(0) == 1, "markTargetReached accepts primary target first hit");
    require((g_playerPlaneFlags & kPrimaryTargetFlag) != 0, "markTargetReached sets primary target flag");
    require(waypointIndex == kPrimaryWaypoint, "markTargetReached advances primary waypoint");
    require(std::strcmp(strBuf, "Primary target") == 0, "markTargetReached writes primary message");
    require(markTargetReached(0) == 0, "markTargetReached rejects repeated primary target hit");

    g_targetSlots[1].state = 2;
    require(markTargetReached(1) == 1, "markTargetReached accepts secondary target hit");
    require((g_playerPlaneFlags & kSecondaryTargetFlag) != 0, "markTargetReached sets secondary target flag");
    require(waypointIndex == kBothTargetsWaypoint, "markTargetReached moves to exit waypoint after both hits");
    require(std::strcmp(strBuf, "Second. target") == 0, "markTargetReached writes secondary message");

    resetGameplayState();
    g_targetSlots[0].state = 4;
    require(markTargetReached(0) == 1 &&
                g_eventLogCount == kExpectedOneCall &&
                g_replayLog.events[0].type == kTargetReachedPrimaryEvent,
            "markTargetReached appends the original primary replay event for terminal target states");

    resetGameplayState();
    g_targetSlots[1].state = 3;
    require(markTargetReached(1) == 1 &&
                g_eventLogCount == kExpectedOneCall &&
                g_replayLog.events[0].type == kTargetReachedSecondaryEvent,
            "markTargetReached appends the original secondary replay event for terminal target states");

    resetGameplayState();
    g_missionTick = 1234;
    g_viewX_ = 0x1234;
    g_viewY_ = 0x5678;
    appendMapEvent(6, 42);
    require(g_eventLogCount == 1, "appendMapEvent increments original replay-log count");
    require(g_replayLog.events[0].coord == g_missionTick &&
                g_replayLog.events[0].screenX == ((unsigned)g_viewX_ >> 7) &&
                g_replayLog.events[0].screenY == ((unsigned)g_viewY_ >> 7) &&
                g_replayLog.events[0].type == 6 && g_replayLog.events[0].arg == 42,
            "appendMapEvent stores original tick, map screen coords, type, and arg");
    require(g_replayLog.events[1].type == kReplayEventTerminator,
            "appendMapEvent writes the following terminator event type");
    g_eventLogCount = kReplayEventLimit;
    g_replayLog.events[kReplayEventLimit].type = 77;
    appendMapEvent(1, 2);
    require(g_eventLogCount == kReplayEventLimit && g_replayLog.events[kReplayEventLimit].type == 77,
            "appendMapEvent ignores events once the original log is full");

    resetGameplayState();
    g_frameRateScaling = 20;
    frameTick = 100;
    scheduleTimedEvent(0x77, 4);
    require(keyValue == 0 && g_directorEventDeadline == -1,
            "scheduleTimedEvent ignores requests when director mode is off");
    g_directorMode = 2;
    scheduleTimedEvent(0x77, 4);
    require(keyValue == 0x77 && g_directorEventDeadline == 180,
            "scheduleTimedEvent stores key and frame-rate-scaled deadline");
    g_directorEventDeadline = -1;
    scheduleEventCheck(0x55, 3);
    require(g_viewTargetObj == 0 && g_directorEventDeadline == -1,
            "scheduleEventCheck rejects priorities above director mode");
    scheduleEventCheck(0x55, 2);
    require(g_viewTargetObj == 0x55 && keyValue == kDirectorKeyValue &&
                g_directorEventDeadline == frameTick + 4 * g_frameRateScaling,
            "scheduleEventCheck schedules mode-2 director event with original delay");
    g_directorMode = 1;
    g_directorEventDeadline = -1;
    scheduleEventCheck(0x66, 1);
    require(g_viewTargetObj == 0x66 &&
                g_directorEventDeadline == frameTick + 3 * g_frameRateScaling,
            "scheduleEventCheck schedules mode-1 director event with shorter delay");
    const int unchangedDeadline = g_directorEventDeadline;
    scheduleEventCheck(0x77, 1);
    require(g_viewTargetObj == 0x66 && g_directorEventDeadline == unchangedDeadline,
            "scheduleEventCheck ignores requests while a director event is pending");

    resetGameplayState();
    g_targetNameTable[2] = const_cast<char *>("Long Primary Radar Site");
    g_targetNameTable[3] = const_cast<char *>("Coastal Airbase");
    g_planeTable.planes[1].nameIndex = 2;
    g_planeTable.planes[0].secondaryNameIndex = 3;
    g_strTruncDot = 0;
    g_strTruncTerm[0] = 'X';
    placeString(1);
    require(std::strcmp(strBuf, "Long Primary Radar Site at Coastal Airbase") == 0,
            "placeString combines primary and secondary target names through original alias");
    require(g_strTruncDot == '.' && g_strTruncTerm[0] == 0,
            "placeString marks long target strings for original truncation");

    struct GameComm comm = {};
    struct Game game = {};
    commData = &comm;
    gameData = &game;

    uint8 nearBytes[] = {1, 2, 3, 4};
    uint8 farBytes[8] = {};
    farPointer = farBytes + 2;
    flagFarToNear = 0;
    moveNearFar(nearBytes, sizeof(nearBytes));
    require(std::memcmp(farBytes + 2, nearBytes, sizeof(nearBytes)) == 0 &&
                farPointer == farBytes + 2 + sizeof(nearBytes),
            "moveNearFar writes near bytes into the original far-buffer cursor");
    uint8 loadedBytes[4] = {};
    farPointer = farBytes + 2;
    flagFarToNear = 1;
    moveNearFar(loadedBytes, sizeof(loadedBytes));
    require(std::memcmp(loadedBytes, nearBytes, sizeof(loadedBytes)) == 0 &&
                farPointer == farBytes + 2 + sizeof(loadedBytes),
            "moveNearFar reads from the original far-buffer cursor into near bytes");
    require(setCommWorldbufPtr() == 0 && farPointer == comm.worldBuf,
            "setCommWorldbufPtr points the transfer cursor at commData worldBuf");

    resetGameplayState();
    comm = {};
    commData = &comm;
    g_viewX_ = 0x1234;
    g_viewY_ = 0x5678;
    g_bombDamageMask = 0x1357;
    g_gunHits = 0x2468;
    g_finalThreatScore = 77;
    g_resupplyCount = 5;
    finalizeMission(0);
    require(g_missionEndedFlag[0] == 1 && comm.bailoutSurvived == 0 &&
                comm.landingType == kLandingSafe,
            "finalizeMission records safe landing outcome");
    require(*reinterpret_cast<int16 *>(reinterpret_cast<char *>(&comm) + 0x74) == g_viewX_ &&
                *reinterpret_cast<int16 *>(reinterpret_cast<char *>(&comm) + 0x76) == g_viewY_ &&
                *reinterpret_cast<int16 *>(reinterpret_cast<char *>(&comm) + 0x34) == g_bombDamageMask &&
                *reinterpret_cast<int16 *>(reinterpret_cast<char *>(&comm) + 0x36) == g_gunHits,
            "finalizeMission writes original debrief handoff fields by DOS offsets");
    require(comm.weaponCount[0] == g_finalThreatScore && comm.weaponCount[1] == g_resupplyCount,
            "finalizeMission stores final threat and resupply counters in weaponCount slots");
    require(g_eventLogCount == 1 && g_replayLog.events[0].type == kMissionEndEvent,
            "finalizeMission appends original mission-end replay event");

    resetGameplayState();
    comm = {};
    commData = &comm;
    finalizeMission(2);
    require(comm.landingType == kLandingCrashed && comm.bailoutSurvived == 2,
            "finalizeMission records crash outcome for nonzero non-eject result");

    resetGameplayState();
    comm = {};
    commData = &comm;
    g_ejectState = 1;
    finalizeMission(0);
    require(comm.landingType == kLandingEjected && comm.bailoutSurvived == 0,
            "finalizeMission records ejected landing type when eject state is active");
    resetGameplayState();
    comm = {};
    commData = &comm;
    g_ejectState = 1;
    finalizeMission(1);
    require(g_missionEndedFlag[0] == 0 && comm.landingType == 0 && g_eventLogCount == 0,
            "finalizeMission ignores nonzero outcomes after ejection started");

    buildRangeString((12 << kRangeKmShift) + 39);
    require(std::strcmp(strBuf, "Range 12.6 km") == 0,
            "buildRangeString preserves original integer km fraction conversion");

    resetGameplayState();
    g_activePanelMode = 0;
    g_groundTargetLock = -1;
    g_airTargetLock = -1;
    g_hudVisible = 0;
    g_planeCount = 0;
    g_groundUnitCount = 0;
    g_wreckAlt = 0;
    updateTargetLock();
    require(g_groundTargetLock == -1 &&
                g_airTargetLock == -1 &&
                g_aamLockActive == 0 &&
                g_lineDrawCalls == 0,
            "updateTargetLock preserves original no-candidate/no-HUD target state");

    resetGameplayState();
    keyValue = kFunction10KeyValue;
    g_aamLockCooldown = kAamCooldownStart;
    g_ViewY = 0x01000000L;
    updateTargetLock();
    require(g_aamLockCooldown == kAamCooldownAfterTick &&
                g_projectSceneObjectCalls >= kExpectedOneCall,
            "updateTargetLock preserves original F10 lock-view aircraft draw and AAM cooldown decrement");

    resetGameplayState();
    g_activePanelMode = kAamPanelMode;
    g_currentWeaponType = kOverlayWeaponGround;
    g_planeCount = kAamCandidateIdx + 1;
    g_computeMapTargetRangeResult = kAamCandidateRange;
    updateTargetLock();
    require(g_groundTargetLock == kAamCandidateIdx &&
                g_lockedTargetKilled == 0,
            "updateTargetLock acquires the original best AAM ground target candidate");

    resetGameplayState();
    g_activePanelMode = kAamPanelMode;
    g_currentWeaponType = kOverlayWeaponGround;
    updateTargetLock();
    require(g_aamLockCooldown == kAamNoCandidateCooldown,
            "updateTargetLock starts the original AAM retry cooldown when no target candidate exists");

    resetGameplayState();
    g_activePanelMode = kAamPanelMode;
    g_currentWeaponType = kOverlayWeaponGround;
    g_planeCount = kAamCandidateIdx + 1;
    g_planeTable.planes[kAamCandidateIdx].active = 1;
    g_targetSlots[0].planeIndex = kAamCandidateIdx;
    g_computeMapTargetRangeResult = kAamCandidateRange + 0x0a00;
    updateTargetLock();
    require(g_groundTargetLock == kAamCandidateIdx,
            "updateTargetLock applies original active-plane and primary-target range biases while selecting AAM targets");

    resetGameplayState();
    g_activePanelMode = kAamPanelMode;
    g_currentWeaponType = kOverlayWeaponGround;
    g_groundTargetLock = kAamDropLockedIdx;
    g_planeCount = 1;
    g_planeTable.planes[kAamCandidateIdx].active = 1;
    g_targetSlots[0].planeIndex = kAamCandidateIdx;
    g_computeMapTargetRangeResult = kAamCandidateRange;
    g_computeTargetBearingValue = kAamOffBoresightBearing;
    updateTargetLock();
    require(g_aamLockActive == 0 &&
                g_groundTargetLock == -1,
            "updateTargetLock drops the original locked AAM target when it leaves the forward cone");

    resetGameplayState();
    g_activePanelMode = kAamPanelMode;
    g_currentWeaponType = kOverlayWeaponGround;
    g_groundTargetLock = kAamDropLockedIdx;
    g_planeCount = 1;
    g_computeMapTargetRangeResult = kAamCandidateRange;
    updateTargetLock();
    require(g_aamLockActive == 1 &&
                g_groundTargetLock == -1,
            "updateTargetLock preserves original AAM active-lock flag before dropping an unrefreshed locked target");

    resetGameplayState();
    frameTick = 1;
    g_groundTargetLock = 0;
    g_activePanelMode = kAamPanelMode;
    g_currentWeaponType = kOverlayWeaponGround;
    g_planeCount = kAamCandidateIdx + 1;
    g_computeMapTargetRangeResult = kAamCandidateRange;
    updateTargetLock();
    require(g_groundTargetLock == 0,
            "updateTargetLock skips original AAM scan on non-sixteenth frames");

    resetGameplayState();
    g_aamLockActive = 1;
    g_groundTargetLock = 0;
    g_activePanelMode = kAamPanelMode;
    g_currentWeaponType = kOverlayWeaponGround;
    g_planeCount = kAamCandidateIdx + 1;
    g_computeMapTargetRangeResult = kAamCandidateRange;
    updateTargetLock();
    require(g_groundTargetLock == 0,
            "updateTargetLock skips original AAM scan while the lock tone is already active");

    resetGameplayState();
    g_groundTargetLock = 0;
    g_activePanelMode = kAamPanelMode;
    g_currentWeaponType = kOverlayWeaponGround;
    updateTargetLock();
    require(g_groundTargetLock == -1 &&
                g_aamLockCooldown == kAamNoCandidateCooldown,
            "updateTargetLock preserves original reset of a stale non-sentinel AAM lock before scanning");

    resetGameplayState();
    g_groundUnitCount = 1;
    g_computeSimObjectRangeResult = kUpdateVisualGroundRange;
    g_computeTargetBearingResult = kUpdateVisualGroundRange;
    g_simObjects[0].flags.b[0] = 2;
    g_simObjects[0].speed = 1;
    g_simObjects[0].posX = kUpdateVisualGroundX;
    g_simObjects[0].posY = kUpdateVisualGroundY;
    updateTargetLock();
    require(g_airTargetLock == 0 &&
                g_lockedTargetKilled == 0,
            "updateTargetLock acquires the original best air-to-ground sim-object lock");

    resetGameplayState();
    g_airTargetLock = kUpdateVisualLockedObj;
    g_computeTargetBearingResult = kUpdateVisualGroundRange;
    g_computeTargetBearingValue = kOverlayOffBoresightBearing;
    updateTargetLock();
    require(g_airTargetLock == -1,
            "updateTargetLock drops the original sentinel A2G lock after an off-boresight range check");

    resetGameplayState();
    g_groundUnitCount = 1;
    g_simObjects[0].flags.b[0] = 0;
    updateTargetLock();
    require(g_airTargetLock == -1 &&
                g_projectSceneObjectCalls == 0,
            "updateTargetLock skips sim objects without the original targetable flag");

    resetGameplayState();
    g_groundUnitCount = 1;
    g_computeSimObjectRangeResult = kUpdateVisualFarRange;
    g_simObjects[0].flags.b[0] = 2;
    updateTargetLock();
    require(g_airTargetLock == -1 &&
                g_projectSceneObjectCalls == 0,
            "updateTargetLock skips far sim objects when director mode is off");

    resetGameplayState();
    g_groundUnitCount = 1;
    g_computeSimObjectRangeResult = kUpdateVisualGroundRange;
    g_computeTargetBearingResult = kUpdateVisualGroundRange;
    g_simObjects[0].flags.b[0] = 2;
    g_simObjects[0].speed = 1;
    pushHudProjection(true, 0, 0, kUpdateVisualPositiveDepth);
    updateTargetLock();
    require(g_airTargetLock == 0 &&
                g_projectSceneObjectCalls == 0 &&
                g_hudLineCalls == 0,
            "updateTargetLock keeps the original positive-depth no-draw sim-object path");

    resetGameplayState();
    g_groundUnitCount = 1;
    g_computeSimObjectRangeResult = kUpdateVisualGroundRange;
    g_computeTargetBearingResult = kUpdateVisualGroundRange;
    g_simObjects[0].flags.b[0] = 2;
    g_simObjects[0].speed = 1;
    pushHudProjection(true, kTargetBoxCenterX, kTargetBoxCenterY, kUpdateVisualFarDotDepth);
    updateTargetLock();
    require(g_airTargetLock == 0 &&
                g_hudLineCalls == kExpectedOneCall &&
                g_lastDrawColor == 0x0f,
            "updateTargetLock draws the original far sim-object HUD dot");

    resetGameplayState();
    g_projectiles[0].ttl = 1;
    pushHudProjection(false, 0, 0, kUpdateVisualDepthNear);
    updateTargetLock();
    require(g_projectSceneObjectCalls == 0 &&
                g_hudLineCalls == 0,
            "updateTargetLock skips original projectile visuals when projection is offscreen");

    resetGameplayState();
    frameTick = 1;
    g_projectiles[0].ttl = 1;
    g_projectiles[0].mapX = kUpdateVisualProjectileX;
    g_projectiles[0].mapY = kUpdateVisualProjectileY;
    g_projectiles[0].alt = kUpdateVisualProjectileAlt;
    g_ViewY = 0x01000000L;
    pushHudProjection(true, 0, 0, kUpdateVisualDepthNear);
    updateTargetLock();
    require(g_projectSceneObjectCalls >= kExpectedOneCall,
            "updateTargetLock draws near original SAM/projectile models instead of far HUD points");

    resetGameplayState();
    frameTick = 1;
    g_ViewY = 0x01000000L;
    g_viewZ = 0x80;
    g_hudVisible = 1;
    g_nearestThreatRange = 0x1000;
    g_particles[0].posX = kUpdateVisualParticleX;
    g_particles[0].posY = kUpdateVisualParticleY;
    g_particles[0].alt = kUpdateVisualParticleAlt;
    g_particles[0].spin = kUpdateVisualParticleSpin;
    g_groundUnitCount = 1;
    g_computeSimObjectRangeResult = kUpdateVisualGroundRange;
    g_computeTargetBearingResult = kUpdateVisualGroundRange;
    g_simObjects[0].flags.b[0] = 2;
    g_simObjects[0].speed = 1;
    g_simObjects[0].posX = kUpdateVisualGroundX;
    g_simObjects[0].posY = kUpdateVisualGroundY;
    g_simObjects[0].alt = kUpdateVisualGroundAlt;
    g_simObjects[0].worldX = kUpdateVisualGroundWorldX;
    g_simObjects[0].worldY = kUpdateVisualGroundWorldY;
    g_planeTable.planes[g_closestThreatIndex].flags = 0x200;
    g_planeTable.planes[g_closestThreatIndex].mapX = kUpdateVisualGroundX;
    g_planeTable.planes[g_closestThreatIndex].mapY = kUpdateVisualGroundY;
    g_attackRangeX = 0x400;
    g_attackRangeY = 0x400;
    g_projectiles[0].ttl = 1;
    g_projectiles[0].mapX = kUpdateVisualProjectileX;
    g_projectiles[0].mapY = kUpdateVisualProjectileY;
    g_projectiles[0].alt = kUpdateVisualProjectileAlt;
    g_wreckX = kUpdateVisualWreckX;
    g_wreckY = kUpdateVisualWreckY;
    g_wreckAlt = kUpdateVisualWreckAlt;
    g_wreckFallVel = 1;
    keyValue = kUpdateVisualPlayerKey;
    pushHudProjection(true, 0, 0, kUpdateVisualDepthNear);
    pushHudProjection(true, 0, 0, kUpdateVisualDepthNear);
    pushHudProjection(true, 0, 0, kUpdateVisualDepthFar);
    pushHudProjection(true, 0, 0, kUpdateVisualDepthNear);
    updateTargetLock();
    require(g_projectSceneObjectCalls >= 6 &&
                g_hudLineCalls >= kExpectedOneCall,
            "updateTargetLock preserves original particle, ground-object, projectile, wreck, and player visual passes");

    resetGameplayState();
    keyValue = kUpdateVisualPlayerKey;
    g_viewZ = 0;
    g_ejectState = 1;
    std::memset(g_particles, 0, sizeof(g_particles));
    updateTargetLock();
    require(g_projectSceneObjectCalls == 0,
            "updateTargetLock preserves original player-fire suppression after ground ejection");

    resetGameplayState();
    g_hudVisible = 0;
    g_targetInHudFlag = 1;
    g_lockedTargetKilled = 1;
    g_hitEffectTimer = 0;
    drawHudWorldOverlay();
    require(g_prevKillMarker == 1 &&
                g_targetInHudFlag == 0 &&
                g_lockedTargetKilled == 0 &&
                g_lineDrawCalls == 0 &&
                g_stringActiveCalls == 0,
            "drawHudWorldOverlay preserves original hidden-HUD cleanup and early return");

    resetGameplayState();
    g_hudVisible = 0;
    drawTargetBox(kTargetBoxCenterX, kTargetBoxCenterY, kTargetBoxSize, 0);
    require(g_hudLineCalls == 0,
            "drawTargetBox preserves the original hidden-HUD no-op");
    g_hudVisible = 1;
    g_halfScaleRender = 0;
    drawTargetBox(kTargetBoxCenterX, kTargetBoxCenterY, kTargetBoxSize, 0);
    require(g_hudLineCalls == 4 &&
                g_hudLines[0].x1 == kTargetBoxCenterX - kTargetBoxSize &&
                g_hudLines[0].y1 == kTargetBoxCenterY - kTargetBoxHalfHeight &&
                g_hudLines[0].x2 == kTargetBoxCenterX - kTargetBoxSize &&
                g_hudLines[0].y2 == kTargetBoxCenterY + kTargetBoxHalfHeight &&
                g_hudLines[3].x1 == kTargetBoxCenterX + kTargetBoxSize &&
                g_hudLines[3].y1 == kTargetBoxCenterY - kTargetBoxHalfHeight,
            "drawTargetBox draws the original rectangular target box geometry");
    resetGameplayState();
    g_hudVisible = 1;
    g_halfScaleRender = 1;
    drawTargetBox(kTargetBoxCenterX, kTargetBoxCenterY, kTargetBoxSize, 1);
    require(g_hudLineCalls == 6 &&
                g_hudLines[0].x1 == kTargetBoxCenterX &&
                g_hudLines[0].y1 == kTargetBoxCenterY - kTargetBoxHalfScaleHalfHeight &&
                g_hudLines[0].x2 == kTargetBoxCenterX + kTargetBoxHalfScaleSize,
            "drawTargetBox halves size and draws the original six-segment lock box in half-scale mode");

    resetGameplayState();
    g_lockToneFlag = 1;
    g_hudVisible = 1;
    g_drawPage = 1;
    drawMissileLock();
    require(g_stringActiveCalls == 1 &&
                std::strcmp(g_lastActiveString, "Missile Lock") == 0 &&
                g_lastActiveStringX == 244 &&
                g_lastActiveStringY == 150 &&
                g_lastActiveStringColor == kMissileLockColor &&
                g_lastDrawColor == kMissileLockColor &&
                g_lineDrawCalls == 2 &&
                g_fullscreenLines[0].x1 == kMissileLockCrossX - 10 &&
                g_fullscreenLines[0].y1 == kMissileLockCrossY &&
                g_fullscreenLines[0].x2 == kMissileLockCrossX + 10 &&
                g_fullscreenLines[1].x1 == kMissileLockCrossX &&
                g_fullscreenLines[1].y1 == kMissileLockCrossY - 8,
            "drawMissileLock draws the original lock text and centered crosshair");
    resetGameplayState();
    g_lockToneFlag = 0;
    g_hudVisible = 1;
    drawMissileLock();
    require(g_lineDrawCalls == 0 && g_stringActiveCalls == 0,
            "drawMissileLock does nothing without the original lock tone flag");

    resetGameplayState();
    g_hudVisible = 1;
    vtxScratch.vproj.x.lo = kTargetBoxCenterX;
    vtxScratch.vproj.y.lo = kTargetBoxCenterY;
    g_scopeArcColor = kTargetLabelColor;
    drawTargetLabel("TEST", kTargetLabelColor, kTargetLabelSize);
    require(g_lastDrawColor == kTargetLabelColor &&
                g_hudLineCalls == 6 &&
                g_stringActiveCalls == 1 &&
                std::strcmp(g_lastActiveString, "TEST") == 0 &&
                g_lastActiveStringX == kTargetLabelTextX &&
                g_lastActiveStringY == kTargetLabelTextY &&
                g_lastActiveStringColor == kTargetLabelColor,
            "drawTargetLabel draws the original box and label while projected point is in bounds");
    resetGameplayState();
    vtxScratch.vproj.x.lo = -1;
    drawTargetLabel("TEST", kTargetLabelColor, kTargetLabelSize);
    require(g_hudLineCalls == 0 && g_stringActiveCalls == 0,
            "drawTargetLabel preserves the original offscreen no-op");

    resetGameplayState();
    g_hudVisible = 1;
    g_frameRateScaling = kOverlayScopeFrameRate;
    g_projectiles[0].ttl = 1;
    pushHudProjection(true, kOverlayProjectileX, kOverlayProjectileY, kOverlayProjectileDepth);
    drawHudWorldOverlay();
    require(g_hudLineCalls == 4 &&
                g_hudLines[0].x1 == kOverlayProjectileX - kOverlayProjectileBoxSize &&
                g_hudLines[0].y1 == kOverlayProjectileY - kOverlayProjectileBoxHalfHeight &&
                g_hudLines[3].x1 == kOverlayProjectileX + kOverlayProjectileBoxSize,
            "drawHudWorldOverlay draws original visible projectile target boxes before HUD panel handling");

    resetGameplayState();
    g_hudVisible = 1;
    g_activePanelMode = kOverlayPanelModeTarget;
    g_currentWeaponType = kOverlayWeaponGround;
    g_groundTargetLock = kOverlayGroundTargetIdx;
    g_planeTable.planes[kOverlayGroundTargetIdx].mapX = kOverlayGroundTargetMapX;
    g_planeTable.planes[kOverlayGroundTargetIdx].mapY = kOverlayGroundTargetMapY;
    g_targetNameTable[0] = const_cast<char *>("SAM");
    g_computeMapTargetRangeResult = kOverlayGroundTargetRangeRaw;
    pushHudProjection(true, kTargetBoxCenterX, kTargetBoxCenterY, kOverlayProjectileDepth);
    drawHudWorldOverlay();
    require(g_projectSceneObjectCalls >= kExpectedOneCall &&
                g_stringActiveCalls >= 2 &&
                g_hudLineCalls >= 10 &&
                std::strcmp(g_lastActiveString, " at SAM") == 0,
            "drawHudWorldOverlay renders original ground-target view, range/name text, and cannon-mode HUD box");

    resetGameplayState();
    g_hudVisible = 1;
    g_activePanelMode = kOverlayPanelModeTarget;
    g_currentWeaponType = kOverlayWeaponAir;
    g_airTargetLock = kOverlayAirTargetIdx;
    g_simObjects[kOverlayAirTargetIdx].spec = kOverlayAirTargetSpec;
    g_simObjects[kOverlayAirTargetIdx].posX = kOverlayAirTargetX;
    g_simObjects[kOverlayAirTargetIdx].posY = kOverlayAirTargetY;
    g_simObjects[kOverlayAirTargetIdx].alt = kOverlayAirTargetAlt;
    drawHudWorldOverlay();
    require(g_projectSceneObjectCalls >= kExpectedOneCall &&
                g_stringActiveCalls >= 2 &&
                std::strcmp(g_lastActiveString, "MIG-23 Flogger") == 0,
            "drawHudWorldOverlay renders original air-target view, range text, and aircraft label");

    resetGameplayState();
    g_hudVisible = 1;
    g_scopeSweepTimer = kOverlayScopeTimer;
    g_frameRateScaling = kOverlayScopeFrameRate;
    g_threatLabelTarget = kOverlayGroundTargetIdx;
    g_planeTable.planes[kOverlayGroundTargetIdx].mapX = kOverlayGroundTargetMapX;
    g_planeTable.planes[kOverlayGroundTargetIdx].mapY = kOverlayGroundTargetMapY;
    g_targetNameTable[0] = const_cast<char *>("SAM");
    pushHudProjection(true, kTargetBoxCenterX, kTargetBoxCenterY, kOverlayProjectileDepth);
    drawHudWorldOverlay();
    require(g_stringActiveCalls >= kExpectedOneCall &&
                g_hudLineCalls >= 6 &&
                std::strcmp(g_lastActiveString, "SAM") == 0,
            "drawHudWorldOverlay draws original positive threat-label target using scope sweep size");

    resetGameplayState();
    g_hudVisible = 1;
    g_scopeSweepTimer = kOverlayScopeTimer;
    g_frameRateScaling = kOverlayScopeFrameRate;
    g_threatLabelTarget = -1 - kOverlayAirTargetIdx;
    g_simObjects[kOverlayAirTargetIdx].spec = kOverlayAirTargetSpec;
    g_simObjects[kOverlayAirTargetIdx].posX = kOverlayAirTargetX;
    g_simObjects[kOverlayAirTargetIdx].posY = kOverlayAirTargetY;
    g_simObjects[kOverlayAirTargetIdx].alt = kOverlayAirTargetAlt;
    pushHudProjection(true, kTargetBoxCenterX, kTargetBoxCenterY, kOverlayProjectileDepth);
    drawHudWorldOverlay();
    require(g_stringActiveCalls >= kExpectedOneCall &&
                g_hudLineCalls >= 6 &&
                std::strcmp(g_lastActiveString, aircraftTypes[kOverlayAirTargetSpec].name) == 0,
            "drawHudWorldOverlay draws original negative threat-label target from sim-object aircraft data");

    resetGameplayState();
    g_hudVisible = 1;
    g_currentWeaponType = kOverlayWeaponAir;
    g_airTargetLock = kOverlayAirTargetIdx;
    missileSpecIndex = 0;
    missleSpec[0].weaponIdx = kFireSidewinderWeapon;
    missleSpec[0].ammo = 1;
    pushHudProjection(true, kOverlayGuidedLockX, kOverlayGuidedLockY, kOverlayAirLockDepth);
    drawHudWorldOverlay();
    require(g_lockToneFlag == 1 &&
                g_hudLineCalls == 6 &&
                g_lastDrawColor == 0x0c,
            "drawHudWorldOverlay preserves original A2A lock tone and six-segment HUD box");

    resetGameplayState();
    g_hudVisible = 1;
    g_currentWeaponType = kOverlayWeaponGroundGuided;
    g_groundTargetLock = kOverlayGroundTargetIdx;
    g_missileTargetCompatResult = 1;
    missileSpecIndex = 0;
    missleSpec[0].weaponIdx = kFireMaverickWeapon;
    missleSpec[0].ammo = 1;
    pushHudProjection(true, kOverlayGuidedLockX, kOverlayGuidedLockY, kOverlayGroundLockDepth);
    drawHudWorldOverlay();
    require(g_lockToneFlag == 1 &&
                g_hudLineCalls == 6 &&
                g_lastDrawColor == 0x0c,
            "drawHudWorldOverlay preserves original guided ground-weapon lock box");

    resetGameplayState();
    g_hudVisible = 1;
    g_currentWeaponType = kOverlayWeaponGroundGuided;
    g_groundTargetLock = kOverlayGroundTargetIdx;
    g_viewZ = 0x1000;
    g_computeMapTargetRangeResult = 1;
    g_missileTargetCompatResult = 1;
    missileSpecIndex = 0;
    missleSpec[0].weaponIdx = kOverlayWeaponPaveway;
    missleSpec[0].ammo = 1;
    pushHudProjection(true, kOverlayGuidedLockX, kOverlayGuidedLockY, kOverlayGroundLockDepth);
    drawHudWorldOverlay();
    require(g_lockToneFlag == 1 &&
                g_hudLineCalls == 6,
            "drawHudWorldOverlay keeps the original Paveway pre-lock exception");

    resetGameplayState();
    g_hudVisible = 1;
    g_currentWeaponType = kOverlayWeaponGroundGuided;
    g_groundTargetLock = kOverlayGroundTargetIdx;
    g_missileTargetCompatResult = 1;
    missileSpecIndex = 0;
    missleSpec[0].weaponIdx = kFireMaverickWeapon;
    missleSpec[0].ammo = 1;
    pushHudProjection(true, kOverlayGuidedLockX + 96, kOverlayGuidedLockY + 64, kOverlayGroundLockDepth);
    drawHudWorldOverlay();
    require(g_lockToneFlag == 0 &&
                g_hudLineCalls == 4,
            "drawHudWorldOverlay clears the original ground lock tone outside the seeker box");

    resetGameplayState();
    g_hudVisible = 1;
    g_unusedHudFlag = 1;
    drawHudWorldOverlay();
    require(g_unusedHudFlag == 0,
            "drawHudWorldOverlay clears the original one-shot unused HUD flag");

    resetGameplayState();
    g_hudVisible = 1;
    g_activePanelMode = kOverlayPanelModeTarget;
    g_currentWeaponType = kOverlayWeaponGroundGuided;
    g_groundTargetLock = kOverlayGroundTargetIdx;
    g_targetSlots[0].planeIndex = kOverlayGroundTargetIdx;
    g_targetNameTable[0] = const_cast<char *>("SAM");
    drawHudWorldOverlay();
    require(std::strcmp(g_lastActiveString, egPrimaryTarget) == 0,
            "drawHudWorldOverlay labels the original primary ground target");

    resetGameplayState();
    g_hudVisible = 1;
    g_activePanelMode = kOverlayPanelModeTarget;
    g_currentWeaponType = kOverlayWeaponGroundGuided;
    g_groundTargetLock = kOverlayGroundTargetIdx;
    g_targetSlots[0].planeIndex = 1;
    g_targetSlots[1].planeIndex = kOverlayGroundTargetIdx;
    g_targetNameTable[0] = const_cast<char *>("SAM");
    drawHudWorldOverlay();
    require(std::strcmp(g_lastActiveString, "Secondary Target") == 0,
            "drawHudWorldOverlay labels the original secondary ground target");

    resetGameplayState();
    g_hudVisible = 1;
    g_activePanelMode = kOverlayPanelModeTarget;
    g_currentWeaponType = kOverlayWeaponGroundGuided;
    g_groundTargetLock = kOverlayGroundTargetIdx;
    g_targetSlots[0].planeIndex = 1;
    g_targetSlots[1].planeIndex = 2;
    g_planeTable.planes[kOverlayGroundTargetIdx].flags = 0x500;
    g_targetNameTable[0] = const_cast<char *>("SAM");
    drawHudWorldOverlay();
    require(std::strcmp(g_lastActiveString, "No Target") == 0,
            "drawHudWorldOverlay preserves the original blinking no-target warning for invalid ground locks");

    resetGameplayState();
    g_hudVisible = 1;
    g_activePanelMode = kOverlayPanelModeTarget;
    g_currentWeaponType = kOverlayWeaponGroundGuided;
    g_groundTargetLock = kOverlayGroundTargetIdx;
    g_targetSlots[0].planeIndex = 1;
    g_targetSlots[1].planeIndex = 2;
    g_planeTable.planes[kOverlayGroundTargetIdx].mapX = 0;
    g_planeTable.planes[kOverlayGroundTargetIdx].mapY = 0;
    g_mapCellFlags[0] = kOverlayInvalidMapCellFlag;
    g_targetNameTable[0] = const_cast<char *>("SAM");
    drawHudWorldOverlay();
    require(std::strcmp(g_lastActiveString, "No Target") == 0,
            "drawHudWorldOverlay preserves the original map-cell no-target warning");

    resetGameplayState();
    g_hudVisible = 1;
    g_activePanelMode = kOverlayPanelModeTarget;
    g_currentWeaponType = kOverlayWeaponGround;
    g_groundTargetLock = kOverlayGroundTargetIdx;
    g_computeTargetBearingValue = kOverlayOffBoresightBearing;
    g_targetNameTable[0] = const_cast<char *>("SAM");
    pushHudProjection(true, kTargetBoxCenterX, kTargetBoxCenterY, kOverlayProjectileDepth);
    drawHudWorldOverlay();
    require(g_groundTargetLock == -1,
            "drawHudWorldOverlay drops the original ground panel lock outside the forward cone");

    resetGameplayState();
    g_hudVisible = 1;
    g_activePanelMode = kOverlayPanelModeTarget;
    g_currentWeaponType = kOverlayWeaponAir;
    g_airTargetLock = kOverlayAirTargetIdx;
    g_simObjects[kOverlayAirTargetIdx].spec = kOverlayNoModelAirTargetSpec;
    drawHudWorldOverlay();
    require(std::strcmp(g_lastActiveString, "No Target") == 0,
            "drawHudWorldOverlay preserves the original no-model aircraft no-target warning");

    resetGameplayState();
    frameTick = 1;
    g_hudVisible = 1;
    g_detailLevel = 1;
    g_activePanelMode = kOverlayPanelModeTarget;
    g_currentWeaponType = kOverlayWeaponAir;
    g_airTargetLock = kOverlayAirTargetIdx;
    g_simObjects[kOverlayAirTargetIdx].spec = kOverlayAirTargetSpec;
    g_simObjects[kOverlayAirTargetIdx].speed = kOverlayAirTargetSpeed;
    g_simObjects[kOverlayAirTargetIdx].pitch = kOverlayAirTargetPitch;
    g_simObjects[kOverlayAirTargetIdx].bank.w = kOverlayAirTargetBank;
    drawHudWorldOverlay();
    require(g_aamLeadDist == kOverlayAirTargetSpeed,
            "drawHudWorldOverlay computes the original odd-frame AAM lead distance");

    resetGameplayState();
    g_hudVisible = 1;
    g_frameRateScaling = kOverlayScopeFrameRate;
    g_bulletTrackCount = 1;
    bulletTracks[0].posX = kUpdateVisualGroundX;
    bulletTracks[0].posY = kUpdateVisualGroundY;
    bulletTracks[0].alt = kUpdateVisualGroundAlt;
    g_groundUnitCount = 1;
    g_simObjects[0].flags.b[0] = 2;
    g_simObjects[0].posX = kUpdateVisualGroundX;
    g_simObjects[0].posY = kUpdateVisualGroundY;
    g_simObjects[0].alt = kUpdateVisualGroundAlt;
    pushHudProjection(true, kTargetBoxCenterX, kTargetBoxCenterY, kOverlayProjectileDepth);
    pushHudProjection(true, kTargetBoxCenterX + 1, kTargetBoxCenterY, kOverlayProjectileDepth);
    drawHudWorldOverlay();
    require(std::strcmp(g_lastHudMessage, "MIG-23 destroyed by gunfire") == 0 &&
                bulletTracks[0].posX == 0 &&
                g_hitEffectTimer == 0,
            "drawHudWorldOverlay preserves original player bullet hit/destroy bookkeeping");

    resetGameplayState();
    g_hudVisible = 1;
    g_frameRateScaling = kOverlayScopeFrameRate;
    bulletTracks[0].posX = kUpdateVisualGroundX;
    bulletTracks[0].posY = kUpdateVisualGroundY;
    bulletTracks[0].alt = g_viewZ;
    g_viewX_ = kUpdateVisualGroundX;
    g_viewY_ = kUpdateVisualGroundY;
    pushHudProjection(true, kTargetBoxCenterX, kTargetBoxCenterY, kOverlayProjectileDepth);
    pushHudProjection(true, kTargetBoxCenterX + 1, kTargetBoxCenterY, kOverlayProjectileDepth);
    drawHudWorldOverlay();
    require(std::strcmp(g_lastHudMessage, "Hit by gunfire") == 0 &&
                g_damageTakenFlag == kExpectedOneCall &&
                g_hitEffectTimer == 0,
            "drawHudWorldOverlay preserves original enemy bullet hit and player damage path");

    resetGameplayState();
    g_hudVisible = 1;
    g_frameRateScaling = kOverlayScopeFrameRate;
    g_bulletTrackCount = 1;
    bulletTracks[0].posX = kUpdateVisualGroundX;
    bulletTracks[0].posY = kUpdateVisualGroundY;
    bulletTracks[0].alt = kOverlayGroundHitAlt;
    pushHudProjection(true, kTargetBoxCenterX, kTargetBoxCenterY, kOverlayProjectileDepth);
    pushHudProjection(true, kTargetBoxCenterX + 1, kTargetBoxCenterY, kOverlayProjectileDepth);
    drawHudWorldOverlay();
    require(bulletTracks[0].posX == 0 &&
                g_hitEffectTimer == 0 &&
                g_hitAlt == kOverlayGroundHitAlt,
            "drawHudWorldOverlay keeps the original ground-impact bullet cleanup");

    resetGameplayState();
    g_hudVisible = 1;
    g_frameRateScaling = kOverlayScopeFrameRate;
    g_bulletTrackCount = 1;
    g_findWaypointEntryResult = kOverlayGroundTargetIdx;
    g_landTargetId[0] = 1;
    g_planeTable.planes[kOverlayGroundTargetIdx].nameIndex = 2;
    g_stubNearestTile.id = 3;
    g_stubNearestTile.x = static_cast<int32>(kUpdateVisualGroundX) << kSpawnWorldShift;
    g_stubNearestTile.y = static_cast<int32>(0x8000 - kUpdateVisualGroundY) << kSpawnWorldShift;
    bulletTracks[0].posX = kUpdateVisualGroundX;
    bulletTracks[0].posY = kUpdateVisualGroundY;
    bulletTracks[0].alt = kOverlayGroundHitAlt;
    pushHudProjection(true, kTargetBoxCenterX, kTargetBoxCenterY, kOverlayProjectileDepth);
    pushHudProjection(true, kTargetBoxCenterX + 1, kTargetBoxCenterY, kOverlayProjectileDepth);
    drawHudWorldOverlay();
    require(g_addTileEntryCalls == kExpectedOneCall &&
                g_hitAlt == 0 &&
                std::strstr(g_lastHudMessage, "destroyed by gunfire") != nullptr,
            "drawHudWorldOverlay preserves original ground-target destruction from bullet impact");

    resetGameplayState();
    g_hitEffectTimer = 1;
    g_hitAlt = kUpdateVisualGroundAlt;
    pushHudProjection(true, kTargetBoxCenterX, kTargetBoxCenterY, kOverlayHitEffectDepth);
    drawHudWorldOverlay();
    require(g_hitEffectTimer == 0 &&
                g_hudLineCalls == kOverlayEffectLineCount,
            "drawHudWorldOverlay renders the original airborne hit-effect burst");

    resetGameplayState();
    g_hudVisible = 1;
    g_hitEffectTimer = 1;
    g_hitAlt = 0;
    g_ourRoll = kFireRoll;
    pushHudProjection(true, kTargetBoxCenterX, kTargetBoxCenterY, kOverlayHitEffectDepth);
    drawHudWorldOverlay();
    require(g_hitEffectTimer == 0 &&
                g_hudLineCalls == kOverlayEffectLineCount,
            "drawHudWorldOverlay renders the original ground hit-effect burst with HUD roll compensation");

    resetGameplayState();
    g_hudVisible = 1;
    g_activePanelMode = kOverlayPanelModeTarget;
    g_currentWeaponType = kOverlayWeaponGround;
    g_groundTargetLock = kOverlayGroundTargetIdx;
    g_hitEffectTimer = 2;
    g_lockedTargetKilled = 1;
    pushHudProjection(false, 0, 0, kOverlayHitEffectDepth);
    drawHudWorldOverlay();
    require(g_blitSpriteCalls == kExpectedOneCall,
            "drawHudWorldOverlay blits the original killed-target marker while the target view is active");

    resetGameplayState();
    g_hudVisible = 1;
    g_activePanelMode = kOverlayPanelModeTarget;
    g_targetInHudFlag = 1;
    drawHudWorldOverlay();
    require(g_fillPanelBoxCalls == kExpectedOneCall,
            "drawHudWorldOverlay clears the original target panel after a target leaves the HUD");

    resetGameplayState();
    g_hudVisible = 1;
    g_currentWeaponType = kOverlayWeaponGroundGuided;
    g_altitude = 1;
    g_flightPathMarkerY = 90;
    missileSpecIndex = 0;
    missleSpec[0].weaponIdx = kOverlayWeaponSlick;
    pushHudProjection(true, kOverlayGuidedLockX + 10, kOverlayGuidedLockY, kOverlayGroundLockDepth);
    drawHudWorldOverlay();
    require(g_hudLineCalls >= kOverlayExpectedReticleLines,
            "drawHudWorldOverlay preserves the original freefall-bomb loft reticle");

    resetGameplayState();
    g_hudVisible = 1;
    g_currentWeaponType = kOverlayWeaponGroundGuided;
    g_groundTargetLock = kOverlayGroundTargetIdx;
    g_altitude = 1;
    g_flightPathMarkerY = 90;
    missileSpecIndex = 0;
    missleSpec[0].weaponIdx = weaponIndexForSpec(kOverlayFreefallBombSpecIndex);
    g_planeTable.planes[kOverlayGroundTargetIdx].mapX = kOverlayGroundTargetMapX;
    g_planeTable.planes[kOverlayGroundTargetIdx].mapY = kOverlayGroundTargetMapY;
    pushHudProjection(false, 0, 0, kOverlayGroundLockDepth);
    pushHudProjection(true, kOverlayGuidedLockX, kOverlayGuidedLockY, kOverlayGroundLockDepth);
    pushHudProjection(true, kOverlayGuidedLockX, kOverlayGuidedLockY, kOverlayGroundLockDepth);
    pushHudProjection(true, kOverlayGuidedLockX, kOverlayGuidedLockY, kOverlayGroundLockDepth);
    pushHudProjection(true, kOverlayGuidedLockX, kOverlayGuidedLockY, kOverlayGroundLockDepth);
    drawHudWorldOverlay();
    require(g_hudLineCalls >= kOverlayFreefallGroundReticleLines &&
                g_lastDrawColor == kOverlayHudWhite,
            "drawHudWorldOverlay preserves original freefall-bomb projected ground reticle");

    resetGameplayState();
    g_hudVisible = 1;
    g_currentWeaponType = kOverlayWeaponGroundGuided;
    g_groundTargetLock = kOverlayGroundTargetIdx;
    g_computeMapTargetRangeResult = kOverlayBombRangeRaw;
    missileSpecIndex = 0;
    missleSpec[0].weaponIdx = kOverlayWeaponRockeye;
    pushHudProjection(true, kOverlayGuidedLockX, kOverlayGuidedLockY, kOverlayGroundLockDepth);
    pushHudProjection(true, kOverlayGuidedLockX, kOverlayGuidedLockY, kOverlayGroundLockDepth);
    pushHudProjection(true, kOverlayGuidedLockX, kOverlayGuidedLockY, kOverlayGroundLockDepth);
    pushHudProjection(true, kOverlayGuidedLockX, kOverlayGuidedLockY, kOverlayGroundLockDepth);
    drawHudWorldOverlay();
    require(g_hudLineCalls >= kOverlayExpectedReticleLines,
            "drawHudWorldOverlay preserves the original retarded-bomb ground reticle");

    resetGameplayState();
    g_threatTimerInit = kThreatAlertTimerInit;
    mapEvents[0].ttl = 1;
    mapEvents[0].mapX = kThreatAlertMapX;
    mapEvents[0].mapY = kThreatAlertMapY;
    g_viewX_ = kThreatAlertViewX;
    g_viewY_ = kThreatAlertViewY;
    g_viewZ = kThreatAlertViewZ;
    g_ourHead = kThreatAlertHead;
    g_planeScanCount = 2;
    g_missionStatus = 1;
    g_difficultyTier = 2;
    g_planeTable.planes[0].active = 1;
    g_planeTable.planes[0].alertLevel = 0;
    g_planeTable.planes[1].active = 1;
    g_planeTable.planes[1].alertLevel = 200;
    updateThreatAlert();
    require(g_threatActiveTimer == kThreatAlertTimerInit &&
                g_threatRefX == kThreatAlertMapX &&
                g_threatRefY == kThreatAlertMapY &&
                g_threatRefZ == kThreatAlertViewZ &&
                g_threatRefHead == kThreatAlertHead &&
                g_unusedEventHist0 == 0xff &&
                g_planeTable.planes[0].alertLevel == kThreatAlertMinAlert &&
                g_planeTable.planes[1].alertLevel == 200,
            "updateThreatAlert copies the original event reference and clamps active alert levels");

    resetGameplayState();
    struct GameComm threatComm = {};
    commData = &threatComm;
    g_viewX_ = 1000;
    g_viewY_ = 1000;
    g_viewZ = 0;
    g_frameRateScaling = 60;
    g_missionStatus = 0;
    g_difficultyTier = 0;
    g_threatScopeRange = 0;
    g_nearestThreatRange = 0x600;
    g_planeTable.planes[0].active = kThreatSiteActiveType;
    g_planeTable.planes[0].mapX = g_viewX_;
    g_planeTable.planes[0].mapY = g_viewY_;
    fireGroundThreat(0);
    require(g_threatToneLevel == kThreatFireToneLow &&
                g_scopeArcRange > 0 &&
                g_scopeSweepTimer == g_frameRateScaling &&
                g_threatLabelTarget == 0 &&
                g_scopeArcColor == kThreatFireToneLow &&
                (g_planeTable.planes[0].flags & 0x14) == 0x14 &&
                g_planeTable.planes[0].alertLevel == 32,
            "fireGroundThreat preserves original low-tone scope alert and alert-level increment");

    resetGameplayState();
    struct GameComm firingThreatComm = {};
    commData = &firingThreatComm;
    g_targetNameTable[0] = const_cast<char *>("");
    g_targetNameTable[1] = const_cast<char *>("Radar");
    g_viewX_ = 0x3000;
    g_viewY_ = 0x3000;
    g_viewZ = 0;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_threatScopeRange = kThreatFireScopeBias;
    g_nearestThreatRange = 0x600;
    g_directorMode = kThreatFireEventPriority;
    g_directorEventDeadline = -1;
    g_planeScanCount = 1;
    g_planeTable.planes[0].active = kThreatFireSa10Type;
    g_planeTable.planes[0].nameIndex = 1;
    g_planeTable.planes[0].mapX = g_viewX_ - kThreatFireRangeX;
    g_planeTable.planes[0].mapY = g_viewY_;
    g_planeTable.planes[0].alertLevel = kThreatFireInitialAlert;
    const int firingBearing = computeBearing(kThreatFireRangeX, 0);
    const int firingRange = rangeApprox(kThreatFireRangeX, 0) >> kRangeKmShift;
    fireGroundThreat(0);
    require(g_threatToneLevel == kThreatFireToneHigh &&
                g_scopeArcRange > 0 &&
                g_scopeArcStart == (firingBearing >> 8) - kThreatScopeArcStartOffset &&
                g_scopeArcEnd == (firingBearing >> 8) + kThreatScopeArcStartOffset &&
                g_scopeArcColor == kThreatFireToneHigh &&
                g_threatRadarFlag == (aNone[kThreatFireSa10Type].flags & 1),
            "fireGroundThreat preserves original high-tone radar arc from bearing and threat flags");
    require(g_planeTable.planes[0].alertLevel == kThreatFireCappedAlert &&
                g_projectiles[0].mapX == g_planeTable.planes[0].mapX + 8 &&
                g_projectiles[0].mapY == g_planeTable.planes[0].mapY &&
                g_projectiles[0].worldX == firingBearing &&
                g_projectiles[0].worldY == kAngleQuarterTurn &&
                g_projectiles[0].specIdx == kThreatFireSa10Type &&
                g_projectiles[0].targetRef == 0 &&
                g_projectiles[0].ttl ==
                    static_cast<int>(((static_cast<long>(sams[kThreatFireSa10Type].lockRange) << 3) *
                                      g_frameRateScaling) /
                                     (sams[kThreatFireSa10Type].maxSpeed >> 6)),
            "fireGroundThreat launches the original SAM projectile once alert and range gates pass");
    require(firingRange > 0 &&
                std::strcmp(g_lastHudMessage, "Radar firing SA-10") == 0 &&
                g_lastAudioSound == kThreatFireSound &&
                g_audioSoundCalls == 1 &&
                g_viewTargetObj == 0x40 &&
                keyValue == kThreatFireDirectorKey &&
                g_directorEventDeadline == frameTick + 4 * g_frameRateScaling &&
                firingThreatComm.restartFlag == 1,
            "fireGroundThreat preserves original launch message, sound, director event, and restart flag");

    resetGameplayState();
    commData = &firingThreatComm;
    g_viewX_ = 0x7000;
    g_viewY_ = 0x3000;
    g_viewZ = 0;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_planeTable.planes[0].active = kThreatSiteActiveType;
    g_planeTable.planes[0].flags = kThreatSiteAttackFlag;
    g_planeTable.planes[0].mapX = g_viewX_ - kThreatFireFarRangeX;
    g_planeTable.planes[0].mapY = g_viewY_;
    g_planeTable.planes[0].alertLevel = 8;
    fireGroundThreat(0);
    require((g_planeTable.planes[0].flags & kThreatSiteAttackFlag) == 0 &&
                g_planeTable.planes[0].alertLevel == 0,
            "fireGroundThreat clears the original attack flag and clamps decayed alerts when range beats score");

    resetGameplayState();
    commData = &threatComm;
    g_hudVisible = 1;
    g_mapMode = 0;
    g_scopeSweepTimer = 0;
    g_prevScopeRange = -1;
    g_threatScopeRange = 1;
    g_targetEntityCount = 1;
    g_frameRateScaling = 60;
    g_planeTable.planes[0].active = kThreatSiteActiveType;
    g_planeTable.planes[0].mapX = g_viewX_;
    g_planeTable.planes[0].mapY = g_viewY_;
    g_planeTable.planes[0].threatTimer = kThreatSiteInitialTimer;
    updateThreatSites();
    require(g_restoreScopeCalls == 1 &&
                g_scopeArcStart == 0 &&
                g_scopeArcEnd == 0x100 &&
                g_planeTable.planes[0].threatTimer >= kThreatSiteResetTimer &&
                g_scopeSweepTimer == -1,
            "updateThreatSites restores the original scope panel and refreshes elapsed site timers");

    resetGameplayState();
    commData = &threatComm;
    g_scopeSweepTimer = -1;
    g_targetEntityCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_planeTable.planes[0].active = kThreatSiteActiveType;
    g_planeTable.planes[0].mapX = g_viewX_;
    g_planeTable.planes[0].mapY = g_viewY_;
    g_planeTable.planes[0].threatTimer = kThreatSiteFireTimer;
    updateThreatSites();
    require((g_planeTable.planes[0].flags & kThreatSiteFireFlag) != 0 &&
                g_planeTable.planes[0].threatTimer == kThreatSiteFireTimer - 1 &&
                g_scopeSweepTimer == kThreatSiteFrameRate - 1 &&
                g_threatToneLevel == kThreatFireToneLow,
            "updateThreatSites fires active sites at the original timer value during negative sweep phases");

    resetGameplayState();
    commData = &threatComm;
    g_hudVisible = 1;
    g_mapMode = 0;
    g_detailLevel = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_scopeSweepTimer = kThreatSiteSweepTimer;
    g_scopeArcRange = kThreatSiteScopeRange;
    g_scopeArcColor = kThreatFireToneMedium;
    g_threatRadarFlag = 1;
    g_scopeArcStart = 7;
    g_scopeArcEnd = 11;
    g_threatLabelTarget = 0;
    g_targetEntityCount = 1;
    g_planeTable.planes[0].active = kThreatSiteActiveType;
    g_planeTable.planes[0].mapX = kThreatAlertMapX;
    g_planeTable.planes[0].mapY = kThreatAlertMapY;
    g_planeTable.planes[0].threatTimer = kThreatSiteFireTimer;
    updateThreatSites();
    require(g_planeTable.planes[0].threatTimer == kThreatSiteFireTimer - 1 &&
                (g_planeTable.planes[0].flags & kThreatSiteFireFlag) == 0 &&
                g_captureScopeCalls == 1 &&
                g_drawMapRangeArcCalls == 1 &&
                g_lastMapArcX == kThreatAlertMapX &&
                g_lastMapArcY == kThreatAlertMapY &&
                g_lastMapArcRadius == kThreatSiteArcRadius &&
                g_lastMapArcColor == kThreatFireToneMedium &&
                g_lastMapArcRadarFlag == 1 &&
                g_lastMapArcStart == 7 &&
                g_lastMapArcEnd == 11 &&
                g_scopeSweepTimer == kThreatSiteSweepTimer - 1,
            "updateThreatSites captures and draws the original detail-mode sweep arc for labeled threats");

    resetGameplayState();
    g_hudVisible = 1;
    g_mapMode = 0;
    g_detailLevel = 0;
    g_scopeSweepTimer = kThreatSiteSweepTimer;
    g_scopeArcRange = kThreatSiteScopeRange;
    g_scopeArcColor = kThreatFireToneLow;
    g_threatLabelTarget = 0;
    g_planeTable.planes[0].mapX = kThreatAlertMapX;
    g_planeTable.planes[0].mapY = kThreatAlertMapY;
    updateThreatSites();
    require(g_captureScopeCalls == 0 &&
                g_drawMapRangeArcCalls == 1 &&
                g_lastMapArcRadius == kThreatSiteLowDetailRadius &&
                g_scopeArcRange == 0 &&
                g_scopeSweepTimer == kThreatSiteSweepTimer - 1,
            "updateThreatSites uses the original full low-detail arc and clears the pending range");

    resetGameplayState();
    commData = &threatComm;
    g_scopeSweepTimer = -1;
    g_targetEntityCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_planeTable.planes[0].active = 0;
    g_planeTable.planes[0].flags = kThreatSiteFireFlag;
    updateThreatSites();
    require((g_planeTable.planes[0].flags & kThreatSiteFireFlag) == 0 &&
                g_scopeSweepTimer == -2,
            "updateThreatSites clears the original site-fired flag when a threat slot becomes inactive");

    resetGameplayState();
    g_smokeSourceIdx = -1;
    frameTick = 2;
    g_activeThreatCount = 3;
    bulletTracks[kThreatObjectBulletSlot].posX = 123;
    g_particles[1].posX = 456;
    updateObjects();
    require(g_enemyThreatCount == 3 &&
                g_activeThreatCount == 0 &&
                bulletTracks[kThreatObjectBulletSlot].posX == 0 &&
                g_particles[1].posX == 0,
            "updateObjects preserves original per-frame empty-object cleanup");

    resetGameplayState();
    g_groundUnitCount = kSpawnGroundUnitCount;
    g_planeScanCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_missionTick = kThreatSpawnMissionTick;
    g_missionStatus = kThreatSpawnMissionStatus;
    g_threatActiveTimer = 1;
    g_threatRefX = kSpawnMapX;
    g_threatRefY = kSpawnMapY;
    g_threatSpec = kSpawnAircraftSpec;
    g_northSouthSign = kSpawnNorthSouthSign;
    g_directorMode = kThreatFireEventPriority;
    g_directorEventDeadline = -1;
    g_targetNameTable[0] = const_cast<char *>("");
    g_simObjects[kThreatSpawnObjIdx].flags.b[0] = 1;
    g_simObjects[kThreatSpawnObjIdx].spec = kSpawnAircraftSpec;
    g_planeTable.planes[0].flags = 1;
    g_planeTable.planes[0].mapX = kSpawnMapX;
    g_planeTable.planes[0].mapY = kSpawnMapY;
    g_planeTable.planes[0].alertLevel = kSpawnAircraftSpec;
    updateObjects();
    require(g_lastSpawnTick == kThreatSpawnMissionTick &&
                (g_simObjects[kThreatSpawnObjIdx].flags.w & kSpawnFlags) == kSpawnFlags &&
                g_simObjects[kThreatSpawnObjIdx].objType == 0 &&
                g_simObjects[kThreatSpawnObjIdx].terrainColor == kSpawnTerrainColor,
            "updateObjects spawns inactive threats through the original random target gate");
    require(g_viewTargetObj == kThreatSpawnDirectorObj &&
                keyValue == kThreatFireDirectorKey &&
                g_directorEventDeadline == frameTick + 4 * g_frameRateScaling,
            "updateObjects schedules the original director event for spawned enemy aircraft");

    resetGameplayState();
    frameTick = kThreatMoveFrameTick;
    g_groundUnitCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_threatActiveTimer = 1;
    g_threatDisplayTtl = 0;
    g_threatRefX = kSpawnMapX;
    g_threatRefY = kSpawnMapY;
    g_threatRefZ = kThreatMoveAlt;
    g_viewX_ = kSpawnMapX;
    g_viewY_ = kSpawnMapY;
    g_viewZ = kThreatMoveAlt;
    g_closestThreatIndex = 0;
    g_simObjects[kThreatMoveObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    updateObjects();
    require(g_activeThreatCount == 1 &&
                g_simObjects[kThreatMoveObjIdx].timer == kThreatMoveTimer - 1 &&
                g_simObjects[kThreatMoveObjIdx].terrainColor == kSpawnTerrainColor &&
                g_readMapPixelCalls == kExpectedOneCall,
            "updateObjects advances an active moving threat through the original mode-1 targeting path");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = 2;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_padlockAircraft = kThreatPadlockTargetIdx;
    g_threatSpec = kSpawnAircraftSpec;
    g_simObjects[kThreatPadlockObjIdx].flags.w = kThreatPadlockFlags;
    g_simObjects[kThreatPadlockObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatPadlockObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatPadlockObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatPadlockObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatPadlockObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatPadlockObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatPadlockObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatPadlockObjIdx].timer = kThreatMoveTimer;
    g_simObjects[kThreatPadlockTargetIdx].heading.w = 0;
    g_simObjects[kThreatPadlockTargetIdx].speed = 10;
    g_simObjects[kThreatPadlockTargetIdx].posX = kSpawnMapX + 20;
    g_simObjects[kThreatPadlockTargetIdx].posY = kSpawnMapY;
    g_simObjects[kThreatPadlockTargetIdx].alt = kThreatMoveAlt;
    updateObjects();
    require(g_simObjects[kThreatPadlockObjIdx].timer == kThreatMoveTimer - 1 &&
                g_simObjects[kThreatPadlockObjIdx].terrainColor == kSpawnTerrainColor &&
                g_readMapPixelCalls == kExpectedOneCall,
            "updateObjects preserves original padlock-targeted moving threat update");

    resetGameplayState();
    frameTick = kThreatModeThreeTick;
    g_groundUnitCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_missionTick = kThreatModeThreeTick;
    g_viewX_ = kSpawnMapX + 7;
    g_viewY_ = kSpawnMapY + 9;
    g_viewZ = kThreatMoveAlt;
    g_threatSpec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].flags.b[0] = 1 | 2 | 8;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatMoveObjIdx].timer == kThreatMoveTimer - 1 &&
                g_simObjects[kThreatMoveObjIdx].terrainColor == kSpawnTerrainColor,
            "updateObjects preserves original mode-3 player-position targeting override");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = kSpawnGroundUnitCount;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_northSouthSign = kSpawnNorthSouthSign;
    g_closestThreatIndex = 0;
    g_threatSpec = kSpawnAircraftSpec;
    g_planeTable.planes[0].mapX = kSpawnMapX;
    g_planeTable.planes[0].mapY = kSpawnMapY;
    g_planeTable.planes[0].flags = kCarrierTargetFlag;
    g_simObjects[kThreatMoveObjIdx].flags.w = kThreatLandingFlags;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].objType = 0;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kSpawnCarrierAltitude;
    g_simObjects[kThreatMoveObjIdx].speed = kSpawnCarrierSpeed;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require((g_simObjects[kThreatMoveObjIdx].flags.w & kThreatLandedFlag) != 0 &&
                g_simObjects[kThreatMoveObjIdx].heading.w == 0 &&
                g_simObjects[kThreatMoveObjIdx].alt == kSpawnCarrierAltitude &&
                g_simObjects[kThreatMoveObjIdx].speed < kSpawnCarrierSpeed,
            "updateObjects preserves original carrier landing-state transition");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_airTargetLock = kThreatMoveObjIdx;
    g_threatSpec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].flags.b[0] = 1 | 2 | kThreatCrashedFlag;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = -1;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_hitEffectTimer == kThreatCrashEffectTimer &&
                g_airTargetLock == -1 &&
                g_smokeParticleSlot == 0 &&
                g_particles[0].posX == kSpawnMapX,
            "updateObjects preserves original crashed-aircraft hit marker and smoke particle");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = 1;
    g_planeScanCount = kThreatTimerRetargetPlaneIdx + 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_threatSpec = kSpawnAircraftSpec;
    g_simObjects[kThreatTimerRetargetObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatTimerRetargetObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatTimerRetargetObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatTimerRetargetObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatTimerRetargetObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatTimerRetargetObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatTimerRetargetObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatTimerRetargetObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatTimerRetargetObjIdx].timer = 1;
    g_planeTable.planes[kThreatTimerRetargetPlaneIdx].flags = 1;
    g_planeTable.planes[kThreatTimerRetargetPlaneIdx].mapX = kSpawnMapX + 1;
    g_planeTable.planes[kThreatTimerRetargetPlaneIdx].mapY = kSpawnMapY;
    updateObjects();
    require((g_simObjects[kThreatTimerRetargetObjIdx].flags.b[0] & 4) != 0 &&
                g_simObjects[kThreatTimerRetargetObjIdx].objType == kThreatTimerRetargetPlaneIdx,
            "updateObjects preserves original timer-expired retarget scan");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = kThreatFarDeactivateObjIdx + 1;
    g_planeCount = kThreatTimerRetargetPlaneIdx + 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_missionTick = kThreatFarDeactivateMissionTick;
    g_viewX_ = 0;
    g_viewY_ = 0;
    g_threatActiveTimer = 0;
    g_threatSpec = kSpawnAircraftSpec;
    g_simObjects[kThreatFarDeactivateObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatFarDeactivateObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatFarDeactivateObjIdx].posX = kThreatFarDeactivatePos;
    g_simObjects[kThreatFarDeactivateObjIdx].posY = kThreatFarDeactivatePos;
    g_simObjects[kThreatFarDeactivateObjIdx].worldX =
        static_cast<long>(kThreatFarDeactivatePos) << kSpawnWorldShift;
    g_simObjects[kThreatFarDeactivateObjIdx].worldY =
        static_cast<long>(kThreatFarDeactivatePos) << kSpawnWorldShift;
    g_simObjects[kThreatFarDeactivateObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatFarDeactivateObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatFarDeactivateObjIdx].timer = kThreatMoveTimer;
    g_planeTable.planes[kThreatTimerRetargetPlaneIdx].flags = 1;
    g_planeTable.planes[kThreatTimerRetargetPlaneIdx].mapX = 1;
    g_planeTable.planes[kThreatTimerRetargetPlaneIdx].mapY = 1;
    updateObjects();
    require((g_simObjects[kThreatFarDeactivateObjIdx].flags.b[0] & 2) == 0 &&
                g_simObjects[kThreatFarDeactivateObjIdx].timer == kThreatFarDeactivateTimerAfterUpdate,
            "updateObjects preserves original far moving-threat deactivation after retarget scan");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_threatActiveTimer = 1;
    g_threatDisplayTtl = 0;
    g_threatRefX = kSpawnMapX + 1;
    g_threatRefY = kSpawnMapY;
    g_threatRefZ = kThreatMoveAlt;
    g_closestThreatIndex = 0;
    g_threatSpec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    g_simObjects[kThreatMoveObjIdx].heading.w = computeBearing(1, 0);
    updateObjects();
    require(bulletTracks[kThreatGunTrackSlot].posX == kSpawnMapX &&
                bulletTracks[kThreatGunTrackSlot].posY == kSpawnMapY &&
                bulletTracks[kThreatGunTrackSlot].alt == kThreatMoveAlt,
            "updateObjects preserves original close-range threat gun track spawn");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = kSpawnGroundUnitCount;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_northSouthSign = kSpawnNorthSouthSign;
    g_closestThreatIndex = 0;
    g_threatSpec = kSpawnAircraftSpec;
    g_planeTable.planes[0].mapX = kSpawnMapX + 0x20;
    g_planeTable.planes[0].mapY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].flags.w = kThreatNormalApproachFlags;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].objType = 0;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatMoveObjIdx].timer == kThreatMoveTimer - 1 &&
                g_simObjects[kThreatMoveObjIdx].terrainColor == kSpawnTerrainColor,
            "updateObjects preserves original normal runway approach target update");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = kSpawnGroundUnitCount;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_threatSpec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].flags.w = kThreatCarrierTakeoffFlags;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].speed = 100;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatMoveObjIdx].speed > 100 &&
                (g_simObjects[kThreatMoveObjIdx].flags.w & 0x400) != 0,
            "updateObjects preserves original carrier takeoff acceleration below climb speed");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = kSpawnGroundUnitCount;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_threatSpec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].flags.w = kThreatCarrierTakeoffFlags;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatHighAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatHighSpeed;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require((g_simObjects[kThreatMoveObjIdx].flags.w & 0x400) == 0,
            "updateObjects clears original carrier takeoff flag after high-speed climb");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_threatSpec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].pitch = kThreatDivePitch;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatMoveObjIdx].pitch > kThreatDivePitch,
            "updateObjects applies original near-ground negative-pitch correction");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_threatActiveTimer = 1;
    g_viewZ = kThreatMoveAlt;
    mapEvents[0].ttl = kThreatMapEventTtl;
    mapEvents[0].mapX = kThreatPadlockMapEventX;
    mapEvents[0].mapY = kThreatPadlockMapEventY;
    g_simObjects[kThreatMoveObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatMoveObjIdx].timer == kThreatMoveTimer - 1 &&
                g_simObjects[kThreatMoveObjIdx].terrainColor == kSpawnTerrainColor,
            "updateObjects preserves original map-event padlock target for active threats");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = kThreatManeuverObjIdx + 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_missionStatus = kThreatManeuverAggressiveStatus;
    g_threatActiveTimer = 1;
    g_threatRefX = kSpawnMapX + kThreatManeuverCloseRange;
    g_threatRefY = kSpawnMapY;
    g_threatRefZ = kThreatMoveAlt;
    {
        const int bearing = computeBearing(kThreatManeuverCloseRange, 0);
        g_simObjects[kThreatManeuverObjIdx].heading.w =
            bearing - (kThreatManeuverRelIndex2 << 13);
        g_ourHead = g_simObjects[kThreatManeuverObjIdx].heading.w;
    }
    g_simObjects[kThreatManeuverObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatManeuverObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatManeuverObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatManeuverObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatManeuverObjIdx].worldX =
        static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatManeuverObjIdx].worldY =
        static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatManeuverObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatManeuverObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatManeuverObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatManeuverObjIdx].pitch > 0,
            "updateObjects preserves original aggressive maneuver-table climb command");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = kThreatManeuverObjIdx + 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_missionStatus = kThreatManeuverAggressiveStatus;
    g_threatActiveTimer = 1;
    g_threatRefX = kSpawnMapX + kThreatManeuverCloseRange;
    g_threatRefY = kSpawnMapY;
    g_threatRefZ = kThreatMoveAlt;
    {
        const int bearing = computeBearing(kThreatManeuverCloseRange, 0);
        g_simObjects[kThreatManeuverObjIdx].heading.w =
            bearing - (kThreatManeuverRelIndex3 << 13);
        g_ourHead = g_simObjects[kThreatManeuverObjIdx].heading.w;
    }
    g_simObjects[kThreatManeuverObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatManeuverObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatManeuverObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatManeuverObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatManeuverObjIdx].worldX =
        static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatManeuverObjIdx].worldY =
        static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatManeuverObjIdx].alt = kThreatNearGroundAlt;
    g_simObjects[kThreatManeuverObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatManeuverObjIdx].pitch = kThreatDivePitch;
    g_simObjects[kThreatManeuverObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatManeuverObjIdx].pitch > kThreatDivePitch,
            "updateObjects preserves original aggressive split-S recovery guard");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = kThreatManeuverObjIdx + 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_missionStatus = kThreatManeuverAggressiveStatus;
    g_threatActiveTimer = 1;
    g_threatRefX = kSpawnMapX + kThreatManeuverCloseRange;
    g_threatRefY = kSpawnMapY;
    g_threatRefZ = kThreatMoveAlt;
    g_simObjects[kThreatManeuverObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatManeuverObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatManeuverObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatManeuverObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatManeuverObjIdx].worldX =
        static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatManeuverObjIdx].worldY =
        static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatManeuverObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatManeuverObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatManeuverObjIdx].bank.w = kThreatManeuverHighBank;
    g_simObjects[kThreatManeuverObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatManeuverObjIdx].timer == kThreatMoveTimer - 1,
            "updateObjects preserves original overbank close-combat command cancellation");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_threatActiveTimer = 1;
    g_activeThreatCount = 1;
    g_threatRefX = kSpawnMapX + kThreatManeuverModeOneRange;
    g_threatRefY = kSpawnMapY;
    g_threatRefZ = kThreatMoveAlt;
    g_planeTable.planes[g_closestThreatIndex].flags = kThreatPlayerCarrierFlag;
    g_nearestThreatRange = kThreatManeuverCloseRange;
    g_simObjects[kThreatMoveObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatMoveObjIdx].bank.w > 0,
            "updateObjects preserves original mode-1 multi-threat and carrier-threat roll override");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_threatActiveTimer = 1;
    g_threatRefX = kSpawnMapX + kThreatManeuverModeOneRange;
    g_threatRefY = kSpawnMapY;
    g_threatRefZ = kThreatMoveAlt;
    g_drawNearestVisibilityByte = kThreatVisibilityByte;
    g_simObjects[kThreatMoveObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require((g_simObjects[kThreatMoveObjIdx].flags.b[1] & kThreatVisibilityFlag) != 0,
            "updateObjects preserves original visible-object high-byte flag update");

    resetGameplayState();
    frameTick = 1; /* Avoid the visibility phase, which can clear the same high-byte flag first. */
    g_groundUnitCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_threatActiveTimer = 1;
    g_threatRefX = kSpawnMapX + kThreatManeuverModeOneRange;
    g_threatRefY = kSpawnMapY;
    g_threatRefZ = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].flags.w = kThreatForcedClimbFlag | 1 | 2;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatMoveObjIdx].pitch > 0,
            "updateObjects preserves original forced-climb pitch command");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_threatActiveTimer = 1;
    g_threatRefX = kSpawnMapX + kThreatManeuverModeOneRange;
    g_threatRefY = kSpawnMapY;
    g_threatRefZ = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatNearGroundAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].pitch = kThreatDivePitch;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatMoveObjIdx].pitch > kThreatDivePitch,
            "updateObjects preserves original near-ground dive recovery with active threat reference");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_threatActiveTimer = 1;
    g_threatRefX = kSpawnMapX + kThreatManeuverModeOneRange;
    g_threatRefY = kSpawnMapY;
    g_threatRefZ = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].pitch = kThreatExtremePitch;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatMoveObjIdx].pitch < kThreatExtremePitch,
            "updateObjects preserves original inverted-pitch wraparound");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_threatActiveTimer = 1;
    g_threatRefX = kSpawnMapX + kThreatManeuverModeOneRange;
    g_threatRefY = kSpawnMapY;
    g_threatRefZ = kThreatHighClampAlt;
    g_simObjects[kThreatMoveObjIdx].flags.b[0] = 1 | 2;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatHighClampAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].pitch = kThreatDivePitch;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatMoveObjIdx].pitch == 0,
            "updateObjects preserves original pitch clear above altitude ceiling");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_northSouthSign = 1;
    g_planeTable.planes[0].mapX = kSpawnMapX;
    g_planeTable.planes[0].mapY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].flags.w = kThreatModeTwoMovingFlags;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].objType = 0;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY + kThreatRunwayOffset;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY =
        static_cast<long>(kSpawnMapY + kThreatRunwayOffset) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].speed = kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require((g_simObjects[kThreatMoveObjIdx].flags.w & kThreatRunwayTouchdownFlag) != 0,
            "updateObjects preserves original runway-approach first touchdown flag");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = kThreatLandingClearGroundUnits;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_northSouthSign = 1;
    g_targetSlots[0].state = kThreatTargetCompleteState;
    g_simObjects[kThreatMoveObjIdx].flags.w = kThreatLandedMovingFlags;
    g_simObjects[kThreatMoveObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatMoveObjIdx].objType = 0;
    g_simObjects[kThreatMoveObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatMoveObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatMoveObjIdx].worldX = static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].worldY = static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatMoveObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatMoveObjIdx].speed = -kThreatMoveSpeed;
    g_simObjects[kThreatMoveObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require(g_simObjects[kThreatMoveObjIdx].flags.w == 0,
            "updateObjects preserves original slot-0 landed-aircraft clear after completed target");

    resetGameplayState();
    frameTick = 0;
    g_groundUnitCount = kThreatLandingRolloutObjIdx + 1;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_northSouthSign = 1;
    g_simObjects[kThreatLandingRolloutObjIdx].flags.w = kThreatLandedMovingFlags;
    g_simObjects[kThreatLandingRolloutObjIdx].spec = kSpawnAircraftSpec;
    g_simObjects[kThreatLandingRolloutObjIdx].objType = 0;
    g_simObjects[kThreatLandingRolloutObjIdx].posX = kSpawnMapX;
    g_simObjects[kThreatLandingRolloutObjIdx].posY = kSpawnMapY;
    g_simObjects[kThreatLandingRolloutObjIdx].worldX =
        static_cast<long>(kSpawnMapX) << kSpawnWorldShift;
    g_simObjects[kThreatLandingRolloutObjIdx].worldY =
        static_cast<long>(kSpawnMapY) << kSpawnWorldShift;
    g_simObjects[kThreatLandingRolloutObjIdx].alt = kThreatMoveAlt;
    g_simObjects[kThreatLandingRolloutObjIdx].speed = -kThreatMoveSpeed;
    g_simObjects[kThreatLandingRolloutObjIdx].timer = kThreatMoveTimer;
    updateObjects();
    require((g_simObjects[kThreatLandingRolloutObjIdx].flags.w & kThreatRolloutFlags) ==
                kThreatRolloutFlags,
            "updateObjects preserves original late-slot landed-aircraft rollout flags");

    resetGameplayState();
    g_posVisibleFlag = 0x7f00;
    testWorldPosVisible(kVisibleWorldX, kVisibleWorldY, kVisibleWorldZ);
    require(static_cast<unsigned char>(g_posVisibleFlag) == 0 &&
                (g_posVisibleFlag & 0xff00) == 0x7f00 &&
                g_drawNearestTileObjectCalls == 1 &&
                g_lastDrawNearestCoord1 ==
                    (static_cast<uint32>(kVisibleWorldX) << kWorldCoordShift) &&
                g_lastDrawNearestCoord2 ==
                    (static_cast<uint32>(kOriginalMapYOffset - kVisibleWorldY)
                     << kWorldCoordShift) &&
                g_lastDrawNearestCoord3 == static_cast<uint32>(kVisibleWorldZ),
            "testWorldPosVisible clears only the low visibility byte and applies original map-to-world transform");

    resetGameplayState();
    updateThreatTargeting();
    require(g_switchIndicatorCalls == 2 &&
                g_lastIndicatorIdx == 1 &&
                g_lastIndicatorColor == kThreatIndicatorSafeColor,
            "updateThreatTargeting resets both original threat indicators when no missiles are active");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 1;
    g_projectiles[kThreatTargetingSlot].ttl = kThreatTargetingTtl;
    g_projectiles[kThreatTargetingSlot].specIdx = kThreatTargetingModeZeroSpec;
    g_projectiles[kThreatTargetingSlot].speed = kSamAcquireSlowSpeed;
    g_projectiles[kThreatTargetingSlot].alt = kThreatTargetingStartAlt;
    updateThreatTargeting();
    require(g_projectiles[kThreatTargetingSlot].speed == kSamAcquireSlowSpeed + 1,
            "updateThreatTargeting preserves original odd-frame SAM speed increment");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 1;
    g_projectiles[kThreatTargetingPlayerSlot].ttl = kThreatTargetingTtl;
    g_projectiles[kThreatTargetingPlayerSlot].specIdx = kThreatTargetingA2aSpec;
    g_projectiles[kThreatTargetingPlayerSlot].speed = kSamAcquireSlowSpeed;
    g_projectiles[kThreatTargetingPlayerSlot].alt = kThreatTargetingStartAlt;
    g_projectiles[kThreatTargetingPlayerSlot].worldX = kSamAcquireHeadingEast;
    updateThreatTargeting();
    require(g_projectiles[kThreatTargetingPlayerSlot].speed == kSamAcquireSlowSpeed + 1,
            "updateThreatTargeting preserves original odd-frame player-missile speed increment");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 0;
    g_directorMode = kThreatFireEventPriority;
    g_directorEventDeadline = -1;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapX =
        kThreatTargetingStartX;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapY =
        kThreatTargetingStartY - kThreatTargetingCloseRange;
    g_projectiles[kThreatTargetingPlayerSlot].ttl = kThreatTargetingTtl;
    g_projectiles[kThreatTargetingPlayerSlot].specIdx = kThreatTargetingGroundSpec;
    g_projectiles[kThreatTargetingPlayerSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingPlayerSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingPlayerSlot].alt = 0;
    g_projectiles[kThreatTargetingPlayerSlot].speed = kSamAcquireSlowSpeed;
    g_projectiles[kThreatTargetingPlayerSlot].targetLock = kThreatTargetingGroundTargetIdx;
    g_projectiles[kThreatTargetingPlayerSlot].worldX = kSamAcquireHeadingNorth;
    updateThreatTargeting();
    require(g_viewTargetObj ==
                kThreatTargetingPlaneDirectorBase + kThreatTargetingGroundTargetIdx,
            "updateThreatTargeting preserves original explicit target-lock director scheduling");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 2;
    g_viewX_ = kThreatTargetingTargetX;
    g_viewY_ = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingSlot].ttl = kThreatTargetingTtl;
    g_projectiles[kThreatTargetingSlot].specIdx = kThreatTargetingModeZeroSpec;
    g_projectiles[kThreatTargetingSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingSlot].alt = kThreatTargetingStartAlt;
    g_projectiles[kThreatTargetingSlot].speed = kThreatTargetingSpeed;
    g_projectiles[kThreatTargetingSlot].worldX =
        computeBearing(kThreatTargetingTargetX - kThreatTargetingStartX, 0);
    updateThreatTargeting();
    require(g_lastIndicatorIdx == 1 && g_lastIndicatorColor == 0x0c,
            "updateThreatTargeting preserves the original radar-lock warning phase");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 0;
    g_viewX_ = kThreatTargetingStartX;
    g_viewY_ = kThreatTargetingStartY;
    g_viewZ = kThreatTargetingStartAlt;
    g_projectiles[kThreatTargetingSlot].ttl = kThreatTargetingTtl;
    g_projectiles[kThreatTargetingSlot].specIdx = kThreatTargetingModeZeroSpec;
    g_projectiles[kThreatTargetingSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingSlot].alt = kThreatTargetingStartAlt;
    g_projectiles[kThreatTargetingSlot].speed = kThreatTargetingPlayerSpeed;
    updateThreatTargeting();
    require(g_projectiles[kThreatTargetingSlot].ttl == 0 &&
                std::strcmp(g_lastHudMessage, "Hit by SA-13") == 0 &&
                g_eventLogCount == kExpectedOneCall &&
                g_replayLog.events[0].type == kThreatTargetingPlayerHitEvent &&
                g_replayLog.events[0].arg == kThreatTargetingModeZeroSpec,
            "updateThreatTargeting preserves original SAM direct-hit player event");

    resetGameplayState();
    g_frameRateScaling = kThreatTargetingGuidanceDecayFrameRate;
    g_projectiles[kThreatTargetingSlot].ttl = kThreatTargetingTtl;
    g_projectiles[kThreatTargetingSlot].specIdx = kThreatTargetingModeZeroSpec;
    g_projectiles[kThreatTargetingSlot].speed = 0;
    g_projectiles[kThreatTargetingSlot].alt = kThreatTargetingStartAlt;
    g_projectiles[kThreatTargetingSlot].worldY = kThreatTargetingGuidanceDecayPitch;
    updateThreatTargeting();
    require(g_projectiles[kThreatTargetingSlot].worldY ==
                kThreatTargetingGuidanceDecayPitch - kThreatTargetingGuidanceDecayStep,
            "updateThreatTargeting preserves original unlocked pitch decay");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    g_projectiles[kThreatTargetingPlayerSlot].ttl = kThreatTargetingTtl;
    g_projectiles[kThreatTargetingPlayerSlot].specIdx = kThreatTargetingGuidedBombSpec;
    g_projectiles[kThreatTargetingPlayerSlot].targetLock = -1;
    g_projectiles[kThreatTargetingPlayerSlot].worldY = 0;
    updateThreatTargeting();
    require(g_projectiles[kThreatTargetingPlayerSlot].worldY ==
                kThreatTargetingMode28PitchClamp,
            "updateThreatTargeting clamps original class-28 guided-bomb pitch");

    resetGameplayState();
    g_projectiles[kThreatTargetingPlayerSlot].ttl = kThreatTargetingTtl;
    g_projectiles[kThreatTargetingPlayerSlot].specIdx = kThreatTargetingFreefallSpec;
    g_frameRateScaling = 1; /* Original one-tick scale makes the class-30 pitch step cross targetRef. */
    g_projectiles[kThreatTargetingPlayerSlot].targetRef = kThreatTargetingMode30TargetRef;
    g_projectiles[kThreatTargetingPlayerSlot].worldY = 0;
    updateThreatTargeting();
    require(g_projectiles[kThreatTargetingPlayerSlot].worldY ==
                kThreatTargetingMode30TargetRef,
            "updateThreatTargeting clamps original class-30 freefall pitch to targetRef");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    g_projectiles[kSamAcquireSlot].mapX = kSamAcquireMapX;
    g_projectiles[kSamAcquireSlot].mapY = kSamAcquireMapY;
    g_projectiles[kSamAcquireSlot].speed = kSamAcquireSlowSpeed;
    g_projectiles[kSamAcquireSlot].ttl = kSamAcquireTtlLarge;
    g_projectiles[kSamAcquireSlot].worldX = kSamAcquireHeadingRear;
    require(samCanAcquireTarget(kSamAcquireSlot, 0, -kSamAcquireTargetRange, 0,
                                kSamAcquireModeRadar) == 0 &&
                g_projectiles[kSamAcquireSlot].ttl == kSamAcquireClampedTtl,
            "samCanAcquireTarget preserves original far-off-boresight ttl clamp for SAM slots");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    g_projectiles[kSamAcquireSlot].mapX = kSamAcquireMapX;
    g_projectiles[kSamAcquireSlot].mapY = kSamAcquireMapY;
    g_projectiles[kSamAcquireSlot].speed = kSamAcquireSlowSpeed;
    g_projectiles[kSamAcquireSlot].worldX = kSamAcquireHeadingEast;
    g_ourHead = kSamAcquireHeadingNorth;
    require(samCanAcquireTarget(kSamAcquireSlot, kSamAcquireTargetRange, 0, 0,
                                kSamAcquireModeRadar) == 0,
            "samCanAcquireTarget preserves original mode-0 player-aspect rejection");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    g_projectiles[kSamAcquireSlot].mapX = kSamAcquireMapX;
    g_projectiles[kSamAcquireSlot].mapY = kSamAcquireMapY;
    g_projectiles[kSamAcquireSlot].speed = kSamAcquireSlowSpeed;
    g_projectiles[kSamAcquireSlot].worldX = kSamAcquireHeadingNorth;
    g_ourHead = kSamAcquireHeadingNorth;
    require(samCanAcquireTarget(kSamAcquireSlot, 0, -kSamAcquireTargetRange, 0,
                                kSamAcquireModeRadar) == 1 &&
                g_acqRange == kSamAcquireTargetRange,
            "samCanAcquireTarget preserves original mode-0 acquisition after player-aspect gate");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    g_projectiles[kSamAcquireSlot].mapX = kSamAcquireMapX;
    g_projectiles[kSamAcquireSlot].mapY = kSamAcquireMapY;
    g_projectiles[kSamAcquireSlot].speed = kSamAcquireSlowSpeed;
    g_projectiles[kSamAcquireSlot].worldX = kSamAcquireHeadingNorth;
    g_ourHead = kSamAcquireHeadingNorth;
    require(samCanAcquireTarget(kSamAcquireSlot, 0, -kSamAcquireTargetRange, 0,
                                kSamAcquireModeHeat) == 1 &&
                g_acqRange == kSamAcquireTargetRange,
            "samCanAcquireTarget preserves original nonzero-mode side-aspect acquisition");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    g_projectiles[kSamAcquireSlot].mapX = kSamAcquireMapX;
    g_projectiles[kSamAcquireSlot].mapY = kSamAcquireMapY;
    g_projectiles[kSamAcquireSlot].speed = kSamAcquireSlowSpeed;
    g_projectiles[kSamAcquireSlot].worldX = kSamAcquireHeadingEast;
    g_ourHead = kSamAcquireHeadingNorth;
    require(samCanAcquireTarget(kSamAcquireSlot, kSamAcquireTargetRange, 0, 0,
                                kSamAcquireModeHeat) == 0,
            "samCanAcquireTarget preserves original nonzero-mode rear-aspect rejection");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 0;
    g_viewX_ = kThreatTargetingTargetX;
    g_viewY_ = kThreatTargetingTargetY;
    g_viewZ = kThreatTargetingStartAlt;
    g_directorMode = kThreatFireEventPriority;
    g_directorEventDeadline = -1;
    g_projectiles[kThreatTargetingSlot].ttl = kThreatTargetingTtl;
    g_projectiles[kThreatTargetingSlot].specIdx = kThreatTargetingSpec;
    g_projectiles[kThreatTargetingSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingSlot].alt = kThreatTargetingStartAlt;
    g_projectiles[kThreatTargetingSlot].speed = kThreatTargetingSpeed;
    g_projectiles[kThreatTargetingSlot].worldX =
        computeBearing(kThreatTargetingTargetX - kThreatTargetingStartX, 0);
    updateThreatTargeting();
    require(g_projectiles[kThreatTargetingSlot].ttl == kThreatTargetingTtl - 1 &&
                (g_projectiles[kThreatTargetingSlot].alt & 1) != 0 &&
                g_projectiles[kThreatTargetingSlot].targetLock == kSpawnTerrainColor,
            "updateThreatTargeting decrements live SAM ttl, marks lock bit, and samples terrain color");
    require(g_switchIndicatorCalls >= 3 &&
                g_lastIndicatorIdx == kThreatTargetingIndicator &&
                g_lastIndicatorColor == kThreatTargetingIndicatorColor &&
                g_lastAudioSound == kThreatTargetingSound &&
                g_viewTargetObj == kThreatTargetingSlot &&
                keyValue == kThreatFireDirectorKey &&
                g_directorEventDeadline == frameTick + 4 * g_frameRateScaling,
            "updateThreatTargeting preserves original lock indicator, warning sound, and director event");
    require(g_readMapPixelCalls == kExpectedOneCall &&
                g_drawNearestTileObjectCalls == kExpectedOneCall,
            "updateThreatTargeting checks original projectile terrain and visibility on matching frame phase");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 1;
    g_ourHead = 0;
    mapEvents[0].ttl = kThreatTargetingCountermeasureTtl;
    mapEvents[0].mapX = kThreatTargetingStartX + kThreatTargetingCloseRange;
    mapEvents[0].mapY = kThreatTargetingStartY;
    mapEvents[kThreatTargetingCountermeasureEventSlot].type = kCountermeasureFlare;
    mapEvents[kThreatTargetingCountermeasureEventSlot].mapX =
        kThreatTargetingStartX + 1;
    mapEvents[kThreatTargetingCountermeasureEventSlot].mapY =
        kThreatTargetingStartY;
    g_projectiles[kThreatTargetingSlot].ttl = kThreatTargetingTtl;
    g_projectiles[kThreatTargetingSlot].specIdx = kThreatTargetingHeatseekerSpec;
    g_projectiles[kThreatTargetingSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingSlot].alt = kThreatTargetingStartAlt;
    g_projectiles[kThreatTargetingSlot].speed = kThreatTargetingPlayerSpeed;
    g_projectiles[kThreatTargetingSlot].worldX = 0;
    updateThreatTargeting();
    require(g_projectiles[kThreatTargetingSlot].ttl == kThreatTargetingTtl - 1 &&
                (g_projectiles[kThreatTargetingSlot].alt & 1) == 0 &&
                g_acqRange == 1,
            "updateThreatTargeting preserves original flare decoy lock break");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 1;
    g_missionStatus = 0;
    g_viewX_ = kThreatTargetingStartX + kThreatTargetingCloseRange;
    g_viewY_ = kThreatTargetingStartY;
    g_viewZ = kThreatTargetingStartAlt;
    mapEvents[kThreatTargetingCountermeasureEventSlot].type = kCountermeasureChaff;
    mapEvents[kThreatTargetingCountermeasureEventSlot].mapX =
        kThreatTargetingStartX + 1;
    mapEvents[kThreatTargetingCountermeasureEventSlot].mapY =
        kThreatTargetingStartY;
    g_projectiles[kThreatTargetingSlot].ttl = kThreatTargetingTtl;
    g_projectiles[kThreatTargetingSlot].specIdx = kThreatTargetingMode3Spec;
    g_projectiles[kThreatTargetingSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingSlot].alt = kThreatTargetingStartAlt;
    g_projectiles[kThreatTargetingSlot].speed = kThreatTargetingPlayerSpeed;
    g_projectiles[kThreatTargetingSlot].worldX =
        computeBearing(kThreatTargetingCloseRange, 0);
    updateThreatTargeting();
    require((g_projectiles[kThreatTargetingSlot].alt & 1) == 0 &&
                g_acqRange == 1,
            "updateThreatTargeting preserves original mode-3 countermeasure retargeting");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 1;
    g_viewX_ = kThreatTargetingStartX + kThreatTargetingStaleRefRange;
    g_viewY_ = kThreatTargetingStartY;
    g_viewZ = kThreatTargetingStartAlt;
    g_ourHead = computeBearing(kThreatTargetingCloseRange, 0);
    g_projectiles[kThreatTargetingSlot].ttl = kThreatTargetingTtl;
    g_projectiles[kThreatTargetingSlot].specIdx = kThreatTargetingModeZeroSpec;
    g_projectiles[kThreatTargetingSlot].targetRef = kThreatTargetingInvalidPlaneRef;
    g_projectiles[kThreatTargetingSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingSlot].alt = kThreatTargetingStartAlt;
    g_projectiles[kThreatTargetingSlot].speed = kThreatTargetingPlayerSpeed;
    g_projectiles[kThreatTargetingSlot].worldX =
        computeBearing(kThreatTargetingCloseRange, 0);
    updateThreatTargeting();
    require((g_projectiles[kThreatTargetingSlot].alt & 1) == 0,
            "updateThreatTargeting clears original SAM lock when plane targetRef lacks live flag");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 1;
    g_viewX_ = kThreatTargetingStartX + kThreatTargetingStaleRefRange;
    g_viewY_ = kThreatTargetingStartY;
    g_viewZ = kThreatTargetingStartAlt;
    g_ourHead = computeBearing(kThreatTargetingCloseRange, 0);
    g_projectiles[kThreatTargetingSlot].ttl = kThreatTargetingTtl;
    g_projectiles[kThreatTargetingSlot].specIdx = kThreatTargetingModeZeroSpec;
    g_projectiles[kThreatTargetingSlot].targetRef = kThreatTargetingInvalidSimRef;
    g_projectiles[kThreatTargetingSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingSlot].alt = kThreatTargetingStartAlt;
    g_projectiles[kThreatTargetingSlot].speed = kThreatTargetingPlayerSpeed;
    g_projectiles[kThreatTargetingSlot].worldX =
        computeBearing(kThreatTargetingCloseRange, 0);
    updateThreatTargeting();
    require((g_projectiles[kThreatTargetingSlot].alt & 1) == 0,
            "updateThreatTargeting clears original SAM lock when sim targetRef lacks lock flag");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 1;
    g_directorMode = kThreatFireEventPriority;
    g_directorEventDeadline = -1;
    g_groundUnitCount = 1;
    g_simObjects[0].flags.b[0] = 2;
    g_simObjects[0].speed = 1;
    g_simObjects[0].spec = kOverlayAirTargetSpec;
    g_simObjects[0].posX = kThreatTargetingStartX + kThreatTargetingCloseRange;
    g_simObjects[0].posY = kThreatTargetingStartY;
    g_simObjects[0].alt = kThreatTargetingStartAlt;
    g_projectiles[kThreatTargetingPlayerSlot].ttl = kThreatTargetingDirectHitTtl;
    g_projectiles[kThreatTargetingPlayerSlot].specIdx = kThreatTargetingA2aSpec;
    g_projectiles[kThreatTargetingPlayerSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingPlayerSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingPlayerSlot].alt = kThreatTargetingStartAlt;
    g_projectiles[kThreatTargetingPlayerSlot].speed = kThreatTargetingPlayerSpeed;
    g_projectiles[kThreatTargetingPlayerSlot].worldX =
        computeBearing(kThreatTargetingCloseRange, 0);
    updateThreatTargeting();
    require(g_projectiles[kThreatTargetingPlayerSlot].ttl == 0 &&
                (g_simObjects[0].flags.b[0] & 0x20) != 0 &&
                std::strcmp(g_lastHudMessage, "MIG-23 hit by AIM120") == 0,
            "updateThreatTargeting preserves original player A2A missile hit handling");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 0;
    g_missileTargetCompatResult = 5;
    g_threatTimerInit = kThreatAlertTimerInit;
    g_planeCount = 1;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapX =
        kThreatTargetingStartX + kThreatTargetingCloseRange;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapY =
        kThreatTargetingStartY;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].flags = 8;
    g_targetNameTable[0] = const_cast<char *>("Depot");
    g_projectiles[kThreatTargetingPlayerSlot].ttl = kThreatTargetingDirectHitTtl;
    g_projectiles[kThreatTargetingPlayerSlot].specIdx = kThreatTargetingGroundSpec;
    g_projectiles[kThreatTargetingPlayerSlot].weaponIdx = kFireMaverickWeapon;
    g_projectiles[kThreatTargetingPlayerSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingPlayerSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingPlayerSlot].alt = 0;
    g_projectiles[kThreatTargetingPlayerSlot].speed = kThreatTargetingPlayerSpeed;
    g_projectiles[kThreatTargetingPlayerSlot].targetLock = kThreatTargetingGroundTargetLock;
    g_projectiles[kThreatTargetingPlayerSlot].worldX =
        computeBearing(kThreatTargetingCloseRange, 0);
    updateThreatTargeting();
    require(g_projectiles[kThreatTargetingPlayerSlot].ttl == 0 &&
                (g_planeTable.planes[kThreatTargetingGroundTargetIdx].nameIndex &
                 0x0100) != 0 &&
                g_threatActiveTimer == kThreatAlertTimerInit &&
                std::strstr(g_lastHudMessage, "hit by AGM-65") != nullptr,
            "updateThreatTargeting preserves original player ground-missile hit handling");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 0;
    g_missileTargetCompatResult = 0;
    g_threatTimerInit = kThreatAlertTimerInit;
    g_planeCount = 1;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapX =
        kThreatTargetingStartX + kThreatTargetingCloseRange;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapY =
        kThreatTargetingStartY;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].flags = 8;
    g_targetNameTable[0] = const_cast<char *>("Depot");
    g_projectiles[kThreatTargetingPlayerSlot].ttl = kThreatTargetingLongHitTtl;
    g_projectiles[kThreatTargetingPlayerSlot].specIdx = kThreatTargetingGroundSpec;
    g_projectiles[kThreatTargetingPlayerSlot].weaponIdx = kFireMaverickWeapon;
    g_projectiles[kThreatTargetingPlayerSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingPlayerSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingPlayerSlot].alt = 0;
    g_projectiles[kThreatTargetingPlayerSlot].speed = kThreatTargetingPlayerSpeed;
    g_projectiles[kThreatTargetingPlayerSlot].targetLock = kThreatTargetingGroundTargetLock;
    g_projectiles[kThreatTargetingPlayerSlot].worldX =
        computeBearing(kThreatTargetingCloseRange, 0);
    updateThreatTargeting();
    require((g_planeTable.planes[kThreatTargetingGroundTargetIdx].nameIndex &
             kTargetDynamicNameFlag) != 0,
            "updateThreatTargeting preserves original long-TTL ground-hit destruction branch");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    frameTick = 0;
    g_missileTargetCompatResult = 0;
    g_planeCount = 1;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapX =
        kThreatTargetingStartX + kThreatTargetingCloseRange;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapY =
        kThreatTargetingStartY;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].flags = 8;
    g_targetNameTable[0] = const_cast<char *>("Depot");
    g_projectiles[kThreatTargetingPlayerSlot].ttl = kThreatTargetingDirectHitTtl;
    g_projectiles[kThreatTargetingPlayerSlot].specIdx = kThreatTargetingGroundSpec;
    g_projectiles[kThreatTargetingPlayerSlot].weaponIdx = kFireMaverickWeapon;
    g_projectiles[kThreatTargetingPlayerSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingPlayerSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingPlayerSlot].alt = 0;
    g_projectiles[kThreatTargetingPlayerSlot].speed = kThreatTargetingPlayerSpeed;
    g_projectiles[kThreatTargetingPlayerSlot].targetLock = kThreatTargetingGroundTargetLock;
    g_projectiles[kThreatTargetingPlayerSlot].worldX =
        computeBearing(kThreatTargetingCloseRange, 0);
    updateThreatTargeting();
    require(std::strcmp(g_lastHudMessage, "Ineffective hit by AGM-65") == 0,
            "updateThreatTargeting preserves original ineffective player ground-hit message");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    g_missionStatus = 0;
    g_loftTargetIdx = kThreatTargetingGroundTargetIdx;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapX = kThreatTargetingStartX;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapY = kThreatTargetingStartY;
    g_targetNameTable[0] = const_cast<char *>("Depot");
    g_projectiles[kThreatTargetingPlayerSlot].ttl = kThreatTargetingDirectHitTtl;
    g_projectiles[kThreatTargetingPlayerSlot].specIdx = kThreatTargetingFreefallSpec;
    g_projectiles[kThreatTargetingPlayerSlot].weaponIdx = kThreatTargetingFreefallWeapon;
    g_projectiles[kThreatTargetingPlayerSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingPlayerSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingPlayerSlot].alt = -1;
    g_projectiles[kThreatTargetingPlayerSlot].targetRef = kFireLoftTargetRef;
    updateThreatTargeting();
    require(g_projectiles[kThreatTargetingPlayerSlot].ttl == 0 &&
                g_hitEffectTimer == kThreatTargetingExplosionTimer &&
                g_hitAlt == 0 &&
                g_savedSamTtl == kThreatTargetingDirectHitTtl - 1 &&
                std::strcmp(g_lastHudMessage, "Depot at Depot destroyed by Slick") == 0,
            "updateThreatTargeting preserves original freefall-bomb direct-hit message quirk");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    g_missionStatus = 0;
    g_loftTargetIdx = kThreatTargetingGroundTargetIdx;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapX =
        kThreatTargetingStartX + kThreatTargetingFreefallMissOffset;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapY =
        kThreatTargetingStartY;
    g_findWaypointEntryResult = kThreatTargetingGroundTargetIdx;
    g_stubNearestTile.x = static_cast<int32>(kThreatTargetingStartX) << kSpawnWorldShift;
    g_stubNearestTile.y = static_cast<int32>(0x8000 - kThreatTargetingStartY) << kSpawnWorldShift;
    g_targetNameTable[0] = const_cast<char *>("Depot");
    g_projectiles[kThreatTargetingPlayerSlot].ttl = kThreatTargetingDirectHitTtl;
    g_projectiles[kThreatTargetingPlayerSlot].specIdx = kThreatTargetingFreefallSpec;
    g_projectiles[kThreatTargetingPlayerSlot].weaponIdx = kThreatTargetingFreefallWeapon;
    g_projectiles[kThreatTargetingPlayerSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingPlayerSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingPlayerSlot].alt = -1;
    g_projectiles[kThreatTargetingPlayerSlot].targetRef = kFireLoftTargetRef;
    updateThreatTargeting();
    require(g_hitEffectTimer == kThreatTargetingExplosionTimer &&
                g_hitAlt == 0 &&
                g_addTileEntryCalls == kExpectedOneCall &&
                std::strstr(g_lastHudMessage, "destroyed by Slick") != nullptr,
            "updateThreatTargeting preserves original nearby-waypoint destruction after a freefall miss");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    g_missionStatus = 0;
    g_loftTargetIdx = kThreatTargetingGroundTargetIdx;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapX =
        kThreatTargetingStartX + kThreatTargetingFreefallMissOffset;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapY =
        kThreatTargetingStartY;
    g_findWaypointEntryResult = -1;
    g_projectiles[kThreatTargetingPlayerSlot].ttl = kThreatTargetingDirectHitTtl;
    g_projectiles[kThreatTargetingPlayerSlot].specIdx = kThreatTargetingFreefallSpec;
    g_projectiles[kThreatTargetingPlayerSlot].weaponIdx = kThreatTargetingFreefallWeapon;
    g_projectiles[kThreatTargetingPlayerSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingPlayerSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingPlayerSlot].alt = -1;
    g_projectiles[kThreatTargetingPlayerSlot].targetRef = kFireLoftTargetRef;
    updateThreatTargeting();
    require(std::strstr(g_lastHudMessage, "misses") != nullptr &&
                g_addTileEntryCalls == 0,
            "updateThreatTargeting preserves original freefall miss with no waypoint match");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    g_missionStatus = 0;
    g_loftTargetIdx = kThreatTargetingGroundTargetIdx;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapX =
        kThreatTargetingStartX + kThreatTargetingFreefallMissOffset;
    g_planeTable.planes[kThreatTargetingGroundTargetIdx].mapY =
        kThreatTargetingStartY;
    g_findWaypointEntryResult = kThreatTargetingGroundTargetIdx;
    g_stubNearestTile.x =
        static_cast<int32>(kThreatTargetingStartX + kThreatTargetingFarWaypointOffset)
        << kSpawnWorldShift;
    g_stubNearestTile.y = static_cast<int32>(0x8000 - kThreatTargetingStartY) << kSpawnWorldShift;
    g_projectiles[kThreatTargetingPlayerSlot].ttl = kThreatTargetingDirectHitTtl;
    g_projectiles[kThreatTargetingPlayerSlot].specIdx = kThreatTargetingFreefallSpec;
    g_projectiles[kThreatTargetingPlayerSlot].weaponIdx = kThreatTargetingFreefallWeapon;
    g_projectiles[kThreatTargetingPlayerSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingPlayerSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingPlayerSlot].alt = -1;
    g_projectiles[kThreatTargetingPlayerSlot].targetRef = kFireLoftTargetRef;
    updateThreatTargeting();
    require(std::strstr(g_lastHudMessage, "misses") != nullptr &&
                g_addTileEntryCalls == 0,
            "updateThreatTargeting preserves original freefall miss when waypoint is outside damage radius");

    resetGameplayState();
    g_frameRateScaling = kFireFrameRateScaling;
    g_projectiles[kThreatTargetingPlayerSlot].ttl = kThreatTargetingDirectHitTtl;
    g_projectiles[kThreatTargetingPlayerSlot].specIdx = kThreatTargetingImpactSpec;
    g_projectiles[kThreatTargetingPlayerSlot].weaponIdx = kThreatTargetingImpactWeapon;
    g_projectiles[kThreatTargetingPlayerSlot].mapX = kThreatTargetingStartX;
    g_projectiles[kThreatTargetingPlayerSlot].mapY = kThreatTargetingStartY;
    g_projectiles[kThreatTargetingPlayerSlot].alt = -1;
    updateThreatTargeting();
    require(g_projectiles[kThreatTargetingPlayerSlot].ttl == 0 &&
                g_hitEffectTimer == static_cast<uint16>(kThreatTargetingMissTimer) &&
                g_lastHudMessage[0] == '\0',
            "updateThreatTargeting preserves original unreachable player missile ground-impact text");

    resetGameplayState();
    g_frameRateScaling = 20;
    frameTick = 100;
    g_mapMode = 0;
    g_currentWeaponType = kGroundTargetWeaponType;
    g_groundTargetLock = kGroundTargetIdx;
    g_enemyGroundRemaining = 1;
    g_targetSlots[0].planeIndex = kGroundTargetIdx;
    g_targetSlots[0].state = 2;
    g_planeTable.planes[kGroundTargetIdx].active = 1;
    g_planeTable.planes[kGroundTargetIdx].flags = kGroundTargetFlagPrimary;
    g_planeTable.planes[kGroundTargetIdx].mapX = kGroundTargetMapX;
    g_planeTable.planes[kGroundTargetIdx].mapY = kGroundTargetMapY;
    g_planeTable.planes[kGroundTargetIdx].nameIndex = kGroundTargetNameIdx;
    g_planeTable.planes[kGroundTargetIdx - 1].secondaryNameIndex = 0;
    g_targetNameTable[0] = const_cast<char *>("");
    g_targetNameTable[kGroundTargetNameIdx] = const_cast<char *>("Radar");
    g_targetNameTable[kGroundTargetMapX] = const_cast<char *>("");
    g_stubTargetSymbol = kGroundTargetShape;
    buf3d3[kGroundTargetShape & 0x7f] = kGroundTargetShapeOffset;
    destroyGroundTarget(kGroundTargetIdx);
    require(g_planeTable.planes[kGroundTargetIdx].active == 0,
            "destroyGroundTarget clears original target active flag");
    require((g_planeTable.planes[kGroundTargetIdx].flags &
             kGroundTargetFlagDestroyed) != 0,
            "destroyGroundTarget sets original destroyed flag");
    require(g_enemyGroundRemaining == 0,
            "destroyGroundTarget decrements original remaining primary-ground count");
    require(g_playerPlaneFlags == kPrimaryTargetFlag,
            "destroyGroundTarget marks the original primary objective reached");
    require(g_smokeSourceIdx == kGroundTargetIdx &&
                g_lockedTargetKilled == 1 &&
                g_lastAudioSound == kDestroyedSoundId,
            "destroyGroundTarget preserves original smoke source, kill marker, and sound");
    require(g_lastNearestWorldX ==
                    (static_cast<uint32>(kGroundTargetMapX) << kWorldCoordShift) &&
                g_lastNearestWorldY ==
                    (static_cast<uint32>(kOriginalMapYOffset - kGroundTargetMapY)
                     << kWorldCoordShift),
            "destroyGroundTarget uses original map-to-world transform for nearest tile lookup");
    require(g_addTileEntryCalls == 1 &&
                g_lastTileEntryValue == kGroundTargetShapeOffset &&
                g_lastTileEntryShape == kGroundTargetShapeTag,
            "destroyGroundTarget replaces the original tile entry with the byte-sized target shape tag");
    require(g_eventLogCount == 1 &&
                g_replayLog.events[0].type == kGroundTargetEventPrimary &&
                g_replayLog.events[0].arg == kGroundTargetEventArg,
            "destroyGroundTarget appends the original primary-target replay event");

    resetGameplayState();
    g_landTargetId[0] = kThreatTargetingLandSymbol;
    g_stubNearestTile.id = kThreatTargetingPlaneZeroTileId;
    g_planeTable.planes[0].mapX = kGroundTargetMapX;
    g_planeTable.planes[0].mapY = kGroundTargetMapY;
    g_targetNameTable[0] = const_cast<char *>("");
    destroyGroundTarget(0);
    require(g_tileKillTally[kThreatTargetingPlaneZeroTileId] == kExpectedOneCall &&
                g_replayLog.events[0].type == 2 &&
                g_replayLog.events[0].arg == kThreatTargetingPlaneZeroTileId,
            "destroyGroundTarget preserves original plane-zero tile kill tally and event");
    require((g_planeTable.planes[0].nameIndex & 0xff) == kThreatTargetingLandSymbol &&
                (g_planeTable.planes[0].nameIndex & 0x0100) != 0 &&
                g_addTileEntryCalls == kExpectedOneCall,
            "destroyGroundTarget writes the original land-symbol name index for plane zero");

    resetGameplayState();
    g_waterTargetId[0] = kThreatTargetingWaterSymbol;
    g_stubNearestTile.id = kThreatTargetingPlaneZeroTileId;
    g_isTargetOverWaterResult = 1;
    g_planeTable.planes[0].mapX = kGroundTargetMapX;
    g_planeTable.planes[0].mapY = kGroundTargetMapY;
    g_targetNameTable[0] = const_cast<char *>("");
    destroyGroundTarget(0);
    require((g_planeTable.planes[0].nameIndex & 0xff) == kThreatTargetingWaterSymbol &&
                (g_planeTable.planes[0].nameIndex & 0x0100) != 0,
            "destroyGroundTarget preserves the original plane-zero water-symbol branch");

    resetGameplayState();
    g_targetNameTable[0] = const_cast<char *>("");
    g_planeTable.planes[kGroundTargetIdx].active = 0;
    g_planeTable.planes[kGroundTargetIdx].flags = 0;
    g_planeTable.planes[kGroundTargetIdx].mapX = kGroundTargetMapX;
    g_planeTable.planes[kGroundTargetIdx].mapY = kGroundTargetMapY;
    g_planeTable.planes[kGroundTargetIdx].nameIndex = 0;
    g_stubTargetSymbol = kGroundTargetShape;
    buf3d3[kGroundTargetShape & 0x7f] = kGroundTargetShapeOffset;
    destroyGroundTarget(kGroundTargetIdx);
    require(g_eventLogCount == kExpectedOneCall &&
                g_replayLog.events[0].type == kGroundTargetEventInactive &&
                g_replayLog.events[0].arg == kGroundTargetIdx,
            "destroyGroundTarget uses the original inactive nonzero-target replay event type");

    resetGameplayState();
    struct GameComm airThreatComm = {};
    commData = &airThreatComm;
    g_viewX_ = 1000;
    g_viewY_ = 1000;
    g_viewZ = 0;
    g_frameRateScaling = 60;
    g_difficultyTier = kAirThreatDifficulty;
    g_missionStatus = kAirThreatMissionStatus;
    g_threatSpec = kAirThreatSpec;
    g_simObjects[kAirThreatObjIdx].posX = g_viewX_;
    g_simObjects[kAirThreatObjIdx].posY = g_viewY_;
    g_simObjects[kAirThreatObjIdx].alt = 0;
    g_simObjects[kAirThreatObjIdx].damage = kAirThreatInitialDamage;
    fireAirThreat(kAirThreatObjIdx);
    require(g_threatToneLevel == kThreatFireToneLow &&
                g_simObjects[kAirThreatObjIdx].damage == kAirThreatDamageStep &&
                (g_simObjects[kAirThreatObjIdx].flags.b[0] &
                 kAirThreatHeatedFlag) != 0 &&
                airThreatComm.restartFlag == 0,
            "fireAirThreat preserves original heat buildup without launching before damage threshold");

    resetGameplayState();
    commData = &airThreatComm;
    g_viewX_ = kAirThreatDecayRangeX;
    g_viewY_ = 0;
    g_viewZ = 0;
    g_threatSpec = kAirThreatSpec;
    g_simObjects[kAirThreatObjIdx].posX = 0;
    g_simObjects[kAirThreatObjIdx].posY = 0;
    g_simObjects[kAirThreatObjIdx].damage = kAirThreatDecayDamage;
    g_simObjects[kAirThreatObjIdx].flags.b[0] = kAirThreatHeatedFlag;
    fireAirThreat(kAirThreatObjIdx);
    require(g_simObjects[kAirThreatObjIdx].damage == 0 &&
                (g_simObjects[kAirThreatObjIdx].flags.b[0] &
                 kAirThreatHeatedFlag) == 0,
            "fireAirThreat clears the original heated flag and clamps damage when the aircraft threat is too far");

    resetGameplayState();
    struct GameComm airThreatLaunchComm = {};
    commData = &airThreatLaunchComm;
    g_viewX_ = 0x3000;
    g_viewY_ = 0x3000;
    g_viewZ = 0;
    frameTick = kFireFrameTick;
    g_frameRateScaling = kThreatSiteFrameRate;
    g_difficultyTier = 0;
    g_missionStatus = kAirThreatLaunchMissionStatus;
    g_threatSpec = kAirThreatLaunchSpec;
    g_enemyThreatCount = 0;
    g_directorMode = kThreatFireEventPriority;
    g_directorEventDeadline = -1;
    g_simObjects[kAirThreatObjIdx].posX = g_viewX_ - kAirThreatLaunchRangeX;
    g_simObjects[kAirThreatObjIdx].posY = g_viewY_;
    g_simObjects[kAirThreatObjIdx].alt = 0;
    g_simObjects[kAirThreatObjIdx].heading.w =
        computeBearing(kAirThreatLaunchRangeX, 0) + kAirThreatLaunchHeadingBias;
    g_simObjects[kAirThreatObjIdx].pitch = kAirThreatLaunchPitch;
    g_simObjects[kAirThreatObjIdx].bank.w = kAirThreatLaunchBank;
    g_simObjects[kAirThreatObjIdx].damage = kAirThreatLaunchDamage;
    g_simObjects[kAirThreatObjIdx].weaponType = kAirThreatLaunchWeapon;
    std::srand(2); /* Drives original randomRange(4) to zero for the post-launch evasive flag branch. */
    const int airThreatBearing = computeBearing(kAirThreatLaunchRangeX, 0);
    const int airThreatRange = rangeApprox(kAirThreatLaunchRangeX, 0) >> kRangeKmShift;
    fireAirThreat(kAirThreatObjIdx);
    require(g_simObjects[kAirThreatObjIdx].damage == 0xff &&
                (g_simObjects[kAirThreatObjIdx].flags.b[0] &
                 kAirThreatHeatedFlag) != 0 &&
                (g_simObjects[kAirThreatObjIdx].flags.b[1] &
                 kAirThreatLaunchedFlag) != 0,
            "fireAirThreat caps heat and marks original launched-air-threat flags");
    require((g_simObjects[kAirThreatObjIdx].flags.b[0] & 4) != 0,
            "fireAirThreat preserves original random post-launch evasive flag branch");
    require(airThreatRange > 8 &&
                g_projectiles[kAirThreatLaunchSlot].mapX ==
                    g_simObjects[kAirThreatObjIdx].posX &&
                g_projectiles[kAirThreatLaunchSlot].mapY ==
                    g_simObjects[kAirThreatObjIdx].posY &&
                g_projectiles[kAirThreatLaunchSlot].alt ==
                    g_simObjects[kAirThreatObjIdx].alt - kAirThreatProjectileAltBias &&
                g_projectiles[kAirThreatLaunchSlot].speed ==
                    (sams[kAirThreatLaunchWeapon].maxSpeed >> kFireTtlSpeedShift) &&
                g_projectiles[kAirThreatLaunchSlot].worldX ==
                    g_simObjects[kAirThreatObjIdx].heading.w &&
                g_projectiles[kAirThreatLaunchSlot].worldY ==
                    g_simObjects[kAirThreatObjIdx].pitch - kAirThreatProjectilePitchBias &&
                g_projectiles[kAirThreatLaunchSlot].worldZ ==
                    g_simObjects[kAirThreatObjIdx].bank.w &&
                g_projectiles[kAirThreatLaunchSlot].specIdx == kAirThreatLaunchWeapon &&
                g_projectiles[kAirThreatLaunchSlot].targetRef == -kAirThreatObjIdx,
            "fireAirThreat seeds the original air-launched missile projectile fields");
    require(g_projectiles[kAirThreatLaunchSlot].ttl ==
                static_cast<int>(((static_cast<long>(sams[kAirThreatLaunchWeapon].lockRange) << 3) *
                                  g_frameRateScaling) /
                                 g_projectiles[kAirThreatLaunchSlot].speed),
            "fireAirThreat preserves the original air-threat missile ttl formula");
    require(airThreatBearing == computeBearing(kAirThreatLaunchRangeX, 0) &&
                std::strcmp(g_lastHudMessage, "AA-6 fired by IL-76") == 0 &&
                g_lastAudioSound == kThreatFireSound &&
                airThreatLaunchComm.restartFlag == 1 &&
                g_viewTargetObj == kAirThreatDirectorObj &&
                keyValue == kThreatFireDirectorKey &&
                g_directorEventDeadline == frameTick + 4 * g_frameRateScaling,
            "fireAirThreat emits the original launch message, sound, restart, and director event");

    resetGameplayState();
    g_wreckAlt = 100;
    g_wreckFallVel = 0;
    applyGravityFall();
    require(g_wreckFallVel == -kWreckGravityStep && g_wreckAlt == 100 - kWreckGravityStep,
            "applyGravityFall applies the original downward acceleration while wreck is above ground");
    g_wreckFallVel = kWreckTerminalFallVelocity;
    g_wreckAlt = 100;
    applyGravityFall();
    require(g_wreckFallVel == kWreckTerminalFallVelocity &&
                g_wreckAlt == 100 + kWreckTerminalFallVelocity,
            "applyGravityFall preserves the original terminal fall velocity clamp");
    g_wreckAlt = 0;
    g_wreckFallVel = 7;
    applyGravityFall();
    require(g_wreckAlt == 0 && g_wreckFallVel == 7,
            "applyGravityFall ignores wrecks already at ground level");

    resetGameplayState();
    g_groundUnitCount = 3;
    for (int idx = 0; idx < g_groundUnitCount; ++idx) {
        g_simObjects[idx].terrainColor = idx;
    }
    g_simObjects[3].terrainColor = 77;
    resetSimObjectLocks();
    require(g_simObjects[0].terrainColor == -1 && g_simObjects[1].terrainColor == -1 &&
                g_simObjects[2].terrainColor == -1 && g_simObjects[3].terrainColor == 77,
            "resetSimObjectLocks clears terrain locks for exactly the original ground-unit count");
    require(g_trackedEnemyIdx == -1, "resetSimObjectLocks clears the tracked-enemy sentinel");

    resetGameplayState();
    struct GameComm loadoutComm = {};
    commData = &loadoutComm;
    loadoutComm.weaponType[0] = 3;
    loadoutComm.weaponType[1] = 5;
    loadoutComm.weaponType[2] = 7;
    loadoutComm.weaponCount[0] = 1;
    loadoutComm.weaponCount[1] = 2;
    loadoutComm.weaponCount[2] = 3;
    g_gunHits = 9;
    g_bombDamageMask = 0x55;
    initWeaponLoadout();
    require(g_gunHits == 0 && g_bombDamageMask == 0,
            "initWeaponLoadout clears original mission hit/damage accumulators");
    for (int idx = 0; idx < 3; ++idx) {
        require(missleSpec[idx].weaponIdx == loadoutComm.weaponType[idx] &&
                    missleSpec[idx].ammo == loadoutComm.weaponCount[idx],
                "initWeaponLoadout copies all three original weapon slots from commData");
    }
    require(g_gunAmmo == kGunAmmoFull && g_fuelRemaining == kFuelFull,
            "initWeaponLoadout restores original full gun and fuel counts");
    require(g_eventTimers[2] == kEventTimerStores && g_eventTimers[1] == kEventTimerFuel,
            "initWeaponLoadout seeds original stores/fuel event timers");
    require(g_fuelGaugeCalls == 1 && g_throttleStateCalls == 1,
            "initWeaponLoadout refreshes original fuel and throttle UI hooks");

    resetGameplayState();
    g_hudVisible = 0;
    missleSpec[0].ammo = 3;
    drawWeaponAmmo();
    require(g_rectRecordCount == 0 &&
                g_numberRecordCount == 0,
            "drawWeaponAmmo preserves original hidden-HUD no-op");

    resetGameplayState();
    g_hudVisible = 1;
    missleSpec[0].ammo = 3;
    missleSpec[1].ammo = 2;
    missleSpec[2].ammo = 1;
    drawWeaponAmmo();
    require(g_rectRecordCount == 3 &&
                g_numberRecordCount == 3 &&
                g_lastDrawColor == kWeaponAmmoClearColor,
            "drawWeaponAmmo clears and redraws all three original HUD weapon counters");
    for (int idx = 0; idx < 3; ++idx) {
        const int x = g_tacmapIndicators[idx];
        require(g_rectRecords[idx].x1 == x - 1 &&
                    g_rectRecords[idx].y1 == kWeaponAmmoHudY &&
                    g_rectRecords[idx].x2 == x + 2 &&
                    g_rectRecords[idx].y2 == kWeaponAmmoClearY2 &&
                    g_numberRecords[idx].value == missleSpec[idx].ammo &&
                    g_numberRecords[idx].x == x &&
                    g_numberRecords[idx].y == kWeaponAmmoHudY &&
                    g_numberRecords[idx].color == kWeaponAmmoColor,
                "drawWeaponAmmo uses the original tac-map indicator X positions and ammo color");
    }

    resetGameplayState();
    g_viewX_ = 0x1234;
    g_viewY_ = 0x5678;
    g_eventTimers[kCountermeasureFlare] = 2;
    g_missionStatus = kCountermeasureTtlMissionStatus;
    g_frameRateScaling = kCountermeasureFrameRate;
    countermeasures(kCountermeasureFlare);
    require(g_eventTimers[kCountermeasureFlare] == 1 &&
                mapEvents[3].mapX == g_viewX_ &&
                mapEvents[3].mapY == g_viewY_ &&
                mapEvents[3].type == kCountermeasureFlare &&
                mapEvents[3].ttl == kCountermeasureExpectedTtl &&
                std::strcmp(g_lastHudMessage, "Flare released") == 0 &&
                std::strcmp(g_lastTimedMessage, "Flare:1") == 0 &&
                g_lastAudioSound == 22,
            "countermeasures consumes a store, emits the original event, HUD text, timed text, and sound");
    g_eventTimers[kCountermeasureChaff] = 0;
    countermeasures(kCountermeasureChaff);
    require(g_eventTimers[kCountermeasureChaff] == 0 &&
                std::strcmp(g_lastHudMessage, "Stores exhausted") == 0,
            "countermeasures clamps exhausted stores and reports the original warning");

    resetGameplayState();
    mapEvents[0].ttl = 2;
    mapEvents[0].type = 7;
    mapEvents[1].ttl = 1;
    mapEvents[1].type = 8;
    tickMessageTimers();
    require(mapEvents[0].ttl == 1 &&
                mapEvents[0].type == 7 &&
                mapEvents[1].ttl == 0 &&
                mapEvents[1].type == 0,
            "tickMessageTimers decrements live events and clears type at zero ttl");

    resetGameplayState();
    struct GameComm frameComm = {};
    commData = &frameComm;
    g_inputDisabled = 0;
    g_bulletTrackCount = 4;
    bulletTracks[0].posX = kBulletInitialPosX;
    bulletTracks[0].posY = kBulletInitialPosY;
    bulletTracks[0].alt = kBulletInitialAlt;
    bulletTracks[0].velX = kBulletVelX;
    bulletTracks[0].velY = kBulletVelY;
    bulletTracks[0].velZ = kBulletVelZ;
    frameTick = kGunFireFrameTick;
    g_axisInputAccum[0] = 1;
    g_gunAmmo = kGunFireAmmoStart;
    g_frameRateScaling = kGunFireFrameRate;
    g_viewX_ = 1000;
    g_viewY_ = 2000;
    g_viewZ = 3000;
    updateBulletsAndFire();
    require(bulletTracks[0].posX == kBulletInitialPosX + kBulletVelX &&
                bulletTracks[0].posY == kBulletInitialPosY + kBulletVelY &&
                bulletTracks[0].alt == kBulletInitialAlt + kBulletVelZ,
            "updateBulletsAndFire advances existing bullet tracks by their velocities");
    require(g_gunAmmo == kGunFireAmmoAfterShot &&
                g_gunFiredFlag == 1 &&
                g_lastAudioSound == kGunFireSound &&
                std::strcmp(g_lastTimedMessage, "GUN:998") == 0,
            "updateBulletsAndFire fires the gun on odd frames with original ammo and HUD message");
    frameTick = 4;
    g_axisInputAccum[0] = 1;
    g_gunFiredFlag = 0;
    g_lastTimedMessage[0] = '\0';
    updateBulletsAndFire();
    require(g_gunFiredFlag == 0 &&
                g_lastTimedMessage[0] == '\0',
            "updateBulletsAndFire preserves the original even-frame no-fire cadence");
    g_axisInputAccum[0] = 0;
    frameTick = 5;
	    updateBulletsAndFire();
	    require(g_gunFiredFlag == 0,
	            "updateBulletsAndFire clears gun-fired flag when the fire input is absent");

	    resetGameplayState();
	    struct Game updateGame = {};
	    struct GameComm updateComm = {};
	    gameData = &updateGame;
	    commData = &updateComm;
	    g_initPhase = kUpdateFrameInitPhaseSteady;
	    g_frameRateScaling = kUpdateFrameScale;
	    g_frameRateAccum = 0;
	    g_missionTick = 0;
	    frameTick = kUpdateFrameTick;
	    g_ViewX = kUpdateFrameWorldX;
	    g_ViewY = kUpdateFrameWorldY;
	    g_viewZ = kUpdateFrameViewZ;
	    g_groundAltitude = 0;
	    g_nearestThreatRange = kUpdateFrameNoThreatRange;
	    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
	    g_smokeSourceIdx = -1;
	    updateFrame();
	    require(g_viewX_ == kUpdateFrameViewX &&
	                g_viewY_ == kUpdateFrameViewY &&
	                g_unusedViewXSnap == kUpdateFrameViewX &&
	                g_unusedViewYSnap == kUpdateFrameViewY,
	            "updateFrame converts original world coordinates to snapped map coordinates");
	    require(g_redrawTacMapCalls == 1 &&
	                g_lastRedrawX == kUpdateFrameViewX &&
	                g_lastRedrawY == kUpdateFrameViewY &&
	                g_gfxCopyRectCalls == 0,
	            "updateFrame redraws the tactical map when the player marker is off screen");
	    require(frameTick == kUpdateFrameExpectedTick &&
	                g_missionTick == 0,
	            "updateFrame advances frame tick without advancing mission tick before the frame-rate divisor");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameTick;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_objectToScreenResult = 1;
    g_objectToScreenX = kUpdateFrameScreenXOutsideScope;
    g_objectToScreenY = kUpdateFrameScreenYOutsideScope;
    g_mapZoomLevel = kUpdateFrameMapZoomStart;
    updateFrame();
    require(g_gfxCopyRectCalls == kExpectedOneCall &&
                g_blitSpriteCalls == kExpectedOneCall &&
                g_mapZoomLevel == kUpdateFrameMapZoomAfterMarker &&
                g_redrawTacMapCalls == kExpectedOneCall,
            "updateFrame preserves original player marker copy, sprite blit, and zoom-out redraw");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameTick;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_directorEventDeadline = frameTick;
    keyValue = kUpdateFrameDirectorKey;
    updateFrame();
    require(keyValue == 0 &&
                g_directorEventDeadline == -1,
            "updateFrame clears the original director key when its deadline expires without autopilot");

    resetGameplayState();
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameTick;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_destroyedCueDeadline = frameTick;
    f15DgtlResult = -1;
    updateFrame();
    require(g_destroyedCueDeadline == 0 &&
                g_lastAudioSample == kUpdateFrameVoiceCueDestroyed,
            "updateFrame plays the original destroyed-target voice cue on its frame deadline");

    resetGameplayState();
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameScale - 1;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    f15DgtlResult = -1;
    updateFrame();
    require(frameTick == kUpdateFrameScale &&
                g_missionTick == 1 &&
                g_lastAudioSample == kUpdateFrameVoiceCueEngineStart &&
                g_engineOnCalls == kExpectedOneCall,
            "updateFrame advances the original one-second mission tick and starts engine audio");

    resetGameplayState();
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    g_frameRateAccum = kUpdateFrameScale * 4 - 1;
    g_frameTimingAccum = kUpdateFrameAlertTimingAccum;
    frameTick = kUpdateFrameTick;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_targetEntityCount = kUpdateFrameAlertPlaneIdx + 1;
    g_planeTable.planes[kUpdateFrameAlertPlaneIdx].alertLevel =
        kUpdateFrameAlertThreshold + 1;
    updateFrame();
    require(g_frameRateAccum == 0 &&
                g_frameTimingAccum == 0 &&
                g_jiffiesPerFrame == kUpdateFrameAlertTimingAccum / kUpdateFrameScale &&
                g_enemyAlertFlag == kExpectedOneCall,
            "updateFrame preserves original periodic jiffies and enemy-alert scan");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    updateGame.unk4 = 1;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameTick;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = 0;
    g_groundAltitude = kUpdateFrameCrashGroundAlt;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_knots = kUpdateFrameCrashKnots;
    updateFrame();
    require(g_missionEndedFlag[0] == 1 &&
                updateComm.landingType == kLandingCrashed &&
                updateComm.bailoutSurvived == 1,
            "updateFrame finalizes the original ground crash outside the landing corridor");

    resetGameplayState();
    struct Game initGame = {};
    struct GameComm initComm = {};
    gameData = &initGame;
    commData = &initComm;
    initGame.difficulty = kUpdateFrameInitDifficultyAutopilot;
    initGame.theater = kUpdateFrameInitTheater;
    initGame.totalScore = 0;
    initComm.setupDetail = 1;
    reinterpret_cast<unsigned char *>(&initComm)[0x0d] = kUpdateFrameTrainingKey;
    g_initPhase = 1;
    g_autopilotEngaged = 0;
    g_frameRateScaling = kUpdateFrameScale;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_groundUnitCount = 6;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    updateFrame();
    require(g_initPhase == kUpdateFrameInitPhaseSteady &&
                initGame.difficulty == kUpdateFrameInitDifficultyAfterAutopilot &&
                g_autopilotEngaged == 1 &&
                (g_playerPlaneFlags & kUpdateFrameAutopilotPlayerFlag) != 0 &&
                (reinterpret_cast<unsigned char *>(&initComm)[0x30] & 1) != 0,
            "updateFrame preserves original difficulty-4 autopilot initialization");
    require(g_axisInputAccum[2] == 1 &&
                g_fuelRemaining == kFuelFull &&
                g_gunAmmo == kGunAmmoFull &&
                g_currentWeaponType == kOverlayWeaponAir &&
                g_frameRateScaling == 15 &&
                g_mapZoomLevel == 1 &&
                g_radarScopeRange == 1 &&
                g_gfxFlipCalls == kExpectedOneCall,
            "updateFrame seeds original first-frame flight, HUD, weapon, and timing state");
    require((g_simObjects[0].flags.b[0] & 2) != 0 &&
                g_simObjects[kUpdateFrameWingmanSlot].speed == 700,
            "updateFrame creates the original training aircraft and wingman setup");

    resetGameplayState();
    struct Game carrierInitGame = {};
    struct GameComm carrierInitComm = {};
    gameData = &carrierInitGame;
    commData = &carrierInitComm;
    carrierInitGame.difficulty = kUpdateFrameInitDifficultyCombat;
    carrierInitGame.theater = kUpdateFrameInitCarrierTheater;
    carrierInitComm.setupDetail = 1;
    g_initPhase = 1;
    g_autopilotEngaged = 0;
    g_frameRateScaling = kUpdateFrameScale;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_groundUnitCount = 6;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_targetSlots[0].viewIndex = 0;
    g_planeTable.planes[0].flags = kUpdateFrameCarrierStartFlag;
    updateFrame();
    require(g_initPhase == kUpdateFrameInitPhaseSteady &&
                g_ViewX == kUpdateFrameWorldX - kUpdateFrameCarrierStartOffset &&
                g_ViewY == kUpdateFrameWorldY &&
                (g_playerPlaneFlags & kUpdateFrameCarrierStartBrakeFlag) != 0,
            "updateFrame preserves original carrier-start X offset and brake flag");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = 0;
    g_ViewX = static_cast<int32>(kUpdateFrameLandingTargetX) << kWorldCoordShift;
    g_ViewY = (kOriginalMapYOffset - static_cast<int32>(kUpdateFrameLandingTargetY))
              << kWorldCoordShift;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = kUpdateFrameViewZ;
    g_nearestThreatRange = 0;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_closestThreatIndex = 0;
    g_planeTable.planes[0].mapX = kUpdateFrameLandingTargetX;
    g_planeTable.planes[0].mapY = kUpdateFrameLandingTargetY;
    g_planeTable.planes[0].flags = kUpdateFrameLandingFlags;
    g_landingTimer = kUpdateFrameLandingTimerStart;
    f15DgtlResult = -1;
    updateFrame();
    require(g_landingTimer == kUpdateFrameLandingTimerAfterSafe &&
                g_landingDoneFlag == 1 &&
                g_gearDownArmed == 1 &&
                g_resupplyCount == kExpectedOneCall &&
                std::strcmp(g_lastHudMessage, "Safe Landing") == 0 &&
                g_lastAudioSample == kUpdateFrameSafeLandingVoiceCue,
            "updateFrame preserves original safe-landing and first resupply bookkeeping");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameAutoLandingScale;
    frameTick = 0;
    g_ViewX = (static_cast<int32>(kUpdateFrameLandingTargetX + 0x40))
              << kWorldCoordShift;
    g_ViewY = (kOriginalMapYOffset - static_cast<int32>(kUpdateFrameLandingTargetY + 0x40))
              << kWorldCoordShift;
    g_altitude = kUpdateFrameViewZ;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = 0;
    g_missionStatus = 0;
    g_playerPlaneFlags = kUpdateFrameAutoLandingFlags;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_closestThreatIndex = 0;
    g_planeTable.planes[0].mapX = kUpdateFrameLandingTargetX;
    g_planeTable.planes[0].mapY = kUpdateFrameLandingTargetY;
    g_planeTable.planes[0].flags = kUpdateFrameLandingFlags;
    updateFrame();
    require(g_autoLandingActive == 0 &&
                g_velocity == 0 &&
                g_lastHudMessage[0] == '\0',
            "updateFrame preserves the original unreachable automatic-landing branch");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameAutoLandingTick;
    g_ViewX = (static_cast<int32>(kUpdateFrameLandingTargetX + kUpdateFrameAutoLandingOffset))
              << kWorldCoordShift;
    g_ViewY = (kOriginalMapYOffset -
               static_cast<int32>(kUpdateFrameLandingTargetY + kUpdateFrameAutoLandingOffset))
              << kWorldCoordShift;
    g_altitude = kUpdateFrameViewZ;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = 0;
    g_missionStatus = 0;
    g_playerPlaneFlags = kUpdateFrameAutoLandingFlags;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_closestThreatIndex = 0;
    g_planeTable.planes[0].mapX = kUpdateFrameLandingTargetX;
    g_planeTable.planes[0].mapY = kUpdateFrameLandingTargetY;
    g_planeTable.planes[0].flags = kUpdateFrameLandingFlags;
    updateFrame();
    require(g_autoLandingActive == 1 &&
                g_velocity == kUpdateFrameAutoLandingSpeed &&
                g_altitude == kUpdateFrameAutoLandingExpectedAlt &&
                g_ViewX == ((static_cast<int32>(kUpdateFrameLandingTargetX +
                                                kUpdateFrameAutoLandingOffset)
                             << kWorldCoordShift) -
                            kUpdateFrameAutoLandingWorldStep) &&
                g_ViewY == ((kOriginalMapYOffset -
                             static_cast<int32>(kUpdateFrameLandingTargetY +
                                                kUpdateFrameAutoLandingOffset))
                            << kWorldCoordShift) +
                               kUpdateFrameAutoLandingWorldStep &&
                std::strcmp(g_lastHudMessage, "Automatic Landing Engaged") == 0,
            "updateFrame preserves original automatic-landing interpolation on non-scan frames");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameAutoLandingTick;
    g_ViewX = static_cast<int32>(kUpdateFrameLandingTargetX) << kWorldCoordShift;
    g_ViewY = (kOriginalMapYOffset - static_cast<int32>(kUpdateFrameLandingTargetY))
              << kWorldCoordShift;
    g_altitude = kUpdateFrameViewZ;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = 0;
    g_missionStatus = 0;
    g_playerPlaneFlags = kUpdateFrameAutoLandingFlags;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_closestThreatIndex = 0;
    g_setThrust = kUpdateFrameAutoLandingSpeed;
    g_planeTable.planes[0].mapX = kUpdateFrameLandingTargetX;
    g_planeTable.planes[0].mapY = kUpdateFrameLandingTargetY;
    g_planeTable.planes[0].flags = kUpdateFrameLandingFlags;
    updateFrame();
    require(g_setThrust == 0 &&
                g_velocity == 0 &&
                g_altitude == 0 &&
                g_ViewX == (static_cast<int32>(kUpdateFrameLandingTargetX) << kWorldCoordShift) &&
                g_ViewY == ((kOriginalMapYOffset - kUpdateFrameLandingTargetY) << kWorldCoordShift),
            "updateFrame preserves original exact automatic-landing snap to the target");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    updateGame.unk4 = 1;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameAutoLandingTick;
    g_ViewX = (static_cast<int32>(kUpdateFrameLandingTargetX + kUpdateFrameCarrierDeckApproachOffset))
              << kWorldCoordShift;
    g_ViewY = (kOriginalMapYOffset -
               static_cast<int32>(kUpdateFrameLandingTargetY + kUpdateFrameCarrierDeckApproachOffset))
              << kWorldCoordShift;
    g_altitude = kUpdateFrameAutoLandingCarrierAlt;
    g_viewZ = kUpdateFrameAutoLandingCarrierAlt;
    g_groundAltitude = 0;
    g_nearestThreatRange = 0;
    g_missionStatus = 0;
    g_playerPlaneFlags = kUpdateFrameAutoLandingFlags;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_closestThreatIndex = 0;
    g_planeTable.planes[0].mapX = kUpdateFrameLandingTargetX;
    g_planeTable.planes[0].mapY = kUpdateFrameLandingTargetY;
    g_planeTable.planes[0].flags = kUpdateFrameCarrierLandingFlags;
    updateFrame();
    require(g_autoLandingActive == 1 &&
                g_groundAltitude == kUpdateFrameCarrierDeckAlt &&
                g_altitude == kUpdateFrameCarrierMinAutoLandingAlt,
            "updateFrame preserves original carrier automatic-landing minimum deck altitude");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameAutoLandingTick;
    g_ViewX = static_cast<int32>(kUpdateFrameLandingTargetX) << kWorldCoordShift;
    g_ViewY = (kOriginalMapYOffset -
               static_cast<int32>(kUpdateFrameLandingTargetY + kUpdateFrameCarrierDeckApproachOffset))
              << kWorldCoordShift;
    g_altitude = kUpdateFrameCarrierDeckAlt;
    g_viewZ = kUpdateFrameCarrierDeckAlt;
    g_groundAltitude = 0;
    g_nearestThreatRange = 0;
    g_northSouthSign = 1;
    g_knots = kUpdateFrameCarrierDeckCrashKnots;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_closestThreatIndex = 0;
    g_planeTable.planes[0].mapX = kUpdateFrameLandingTargetX;
    g_planeTable.planes[0].mapY = kUpdateFrameLandingTargetY;
    g_planeTable.planes[0].flags = kUpdateFrameCarrierLandingFlags;
    updateFrame();
    require(g_autoCrashDive == 1 &&
                g_lastAudioSound == kUpdateFrameCarrierDeckCrashSound,
            "updateFrame preserves original carrier deck overrun auto-crash trigger");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameTick;
    g_ViewX = 0;
    g_ViewY = 0;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_threatActiveTimer = kUpdateFrameThreatTimerStart;
    updateFrame();
    require(g_viewX_ == kUpdateFrameMinViewX &&
                g_viewY_ == kUpdateFrameMaxViewY &&
                g_ViewX == (static_cast<int32>(kUpdateFrameMinViewX) << kWorldCoordShift) &&
                g_ViewY == ((kOriginalMapYOffset - kUpdateFrameMaxViewY) << kWorldCoordShift) &&
                g_threatActiveTimer == kUpdateFrameThreatTimerStart - 1,
            "updateFrame clamps original world-to-map bounds and decrements the threat-active timer");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = 0;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_planeCount = kUpdateFrameCarrierThreatIdx + 1;
    g_groundUnitCount = kUpdateFrameGroundUnitsForThreat;
    g_closestThreatIndex = 0;
    g_prevThreatIndex = 0;
    g_northSouthSign = 1;
    g_planeTable.planes[0].mapX = kUpdateFrameViewX + 0x200;
    g_planeTable.planes[0].mapY = kUpdateFrameViewY + 0x200;
    g_planeTable.planes[0].flags = kUpdateFrameLandingFlags;
    g_planeTable.planes[kUpdateFrameCarrierThreatIdx].mapX = kUpdateFrameCarrierX;
    g_planeTable.planes[kUpdateFrameCarrierThreatIdx].mapY = kUpdateFrameCarrierY;
    g_planeTable.planes[kUpdateFrameCarrierThreatIdx].flags = kUpdateFrameCarrierFlags;
    updateFrame();
    require(g_closestThreatIndex == kUpdateFrameCarrierThreatIdx &&
                g_targetSlots[1].viewIndex == kUpdateFrameCarrierThreatIdx &&
                waypoints[3].mapX == kUpdateFrameCarrierX &&
                waypoints[3].mapY == kUpdateFrameCarrierY,
            "updateFrame rescans aircraft every eighth frame and retargets the nearest original threat");
    require(g_simObjects[kUpdateFrameGroundUnitsForThreat - 1].spec == kUpdateFrameCarrierSpec &&
                g_simObjects[kUpdateFrameGroundUnitsForThreat - 2].spec == kUpdateFrameCarrierSpec &&
                g_simObjects[kUpdateFrameGroundUnitsForThreat - 3].spec == kUpdateFrameCarrierEscortSpec &&
                g_simObjects[kUpdateFrameGroundUnitsForThreat - 4].spec == kUpdateFrameCarrierEscortSpec &&
                g_simObjects[kUpdateFrameGroundUnitsForThreat - 3].alt == kUpdateFrameCarrierAlt &&
                g_simObjects[kUpdateFrameGroundUnitsForThreat - 4].alt == kUpdateFrameCarrierAlt &&
                g_simObjects[kUpdateFrameGroundUnitsForThreat - 3].posX == kUpdateFrameCarrierX + kUpdateFrameCarrierOffset &&
                g_simObjects[kUpdateFrameGroundUnitsForThreat - 4].posY == kUpdateFrameCarrierY,
            "updateFrame rebuilds the original carrier-threat sim objects with carrier offsets and specs");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = 0;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_planeCount = kUpdateFrameCarrierThreatIdx + 1;
    g_groundUnitCount = kUpdateFrameGroundUnitsForThreat;
    g_closestThreatIndex = 0;
    g_prevThreatIndex = 0;
    g_northSouthSign = 1;
    g_planeTable.planes[0].mapX = kUpdateFrameViewX + 0x200;
    g_planeTable.planes[0].mapY = kUpdateFrameViewY + 0x200;
    g_planeTable.planes[0].flags = kUpdateFrameLandingFlags;
    g_planeTable.planes[kUpdateFrameCarrierThreatIdx].mapX = kUpdateFrameNonCarrierX;
    g_planeTable.planes[kUpdateFrameCarrierThreatIdx].mapY = kUpdateFrameNonCarrierY;
    g_planeTable.planes[kUpdateFrameCarrierThreatIdx].flags = kUpdateFrameNonCarrierFlags;
    updateFrame();
    require(g_simObjects[kUpdateFrameGroundUnitsForThreat - 3].posX ==
                    kUpdateFrameNonCarrierX + kUpdateFrameNonCarrierOffsetX &&
                g_simObjects[kUpdateFrameGroundUnitsForThreat - 3].posY ==
                    kUpdateFrameNonCarrierY &&
                g_simObjects[kUpdateFrameGroundUnitsForThreat - 4].posY ==
                    kUpdateFrameNonCarrierY + kUpdateFrameNonCarrierOffsetY &&
                g_simObjects[kUpdateFrameGroundUnitsForThreat - 3].alt ==
                    kUpdateFrameNonCarrierAlt &&
                g_simObjects[kUpdateFrameGroundUnitsForThreat - 4].alt ==
                    kUpdateFrameNonCarrierAlt,
            "updateFrame rebuilds original non-carrier threat objects with land-target offsets");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameTick;
    g_ViewX = static_cast<int32>(kUpdateFrameLandingTargetX) << kWorldCoordShift;
    g_ViewY = (kOriginalMapYOffset - static_cast<int32>(kUpdateFrameLandingTargetY))
              << kWorldCoordShift;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = 0;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_closestThreatIndex = 0;
    g_planeTable.planes[0].mapX = kUpdateFrameLandingTargetX;
    g_planeTable.planes[0].mapY = kUpdateFrameLandingTargetY;
    g_planeTable.planes[0].flags = kUpdateFrameAlternateAttackFlags;
    updateFrame();
    require(g_attackRangeY == kUpdateFrameAlternateAttackRangeY,
            "updateFrame preserves original alternate attack range for non-landing targets");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = 0;
    g_ViewX = static_cast<int32>(kUpdateFrameLandingTargetX) << kWorldCoordShift;
    g_ViewY = (kOriginalMapYOffset - static_cast<int32>(kUpdateFrameLandingTargetY))
              << kWorldCoordShift;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = kUpdateFrameViewZ;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_closestThreatIndex = 0;
    g_playerPlaneFlags = kUpdateFrameAutoLandingFlags;
    g_landingTimer = kUpdateFrameScale + 1;
    g_planeTable.planes[0].mapX = kUpdateFrameLandingTargetX;
    g_planeTable.planes[0].mapY = kUpdateFrameLandingTargetY;
    g_planeTable.planes[0].flags = kUpdateFrameLandingFlags;
    updateFrame();
    require(g_missionEndedFlag[0] == 1 &&
                updateComm.landingType == kLandingSafe,
            "updateFrame finalizes the original safe landing once the armed landing timer exceeds the frame scale");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameLandingWeaponsTick;
    g_ViewX = static_cast<int32>(kUpdateFrameLandingTargetX) << kWorldCoordShift;
    g_ViewY = (kOriginalMapYOffset - static_cast<int32>(kUpdateFrameLandingTargetY))
              << kWorldCoordShift;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = kUpdateFrameViewZ;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_closestThreatIndex = 0;
    g_landingTimer = kUpdateFrameLandingMessageTimer;
    g_planeTable.planes[0].mapX = kUpdateFrameLandingTargetX;
    g_planeTable.planes[0].mapY = kUpdateFrameLandingTargetY;
    g_planeTable.planes[0].flags = kUpdateFrameLandingFlags;
    updateFrame();
    require(g_landingTimer == kUpdateFrameLandingTimerAfterResupply &&
                std::strcmp(g_lastHudMessage, "Weapons replenished") == 0,
            "updateFrame preserves original non-auto landing resupply message on frameTick bit 3 clear");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameLandingReadyTick;
    g_ViewX = static_cast<int32>(kUpdateFrameLandingTargetX) << kWorldCoordShift;
    g_ViewY = (kOriginalMapYOffset - static_cast<int32>(kUpdateFrameLandingTargetY))
              << kWorldCoordShift;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = kUpdateFrameViewZ;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_closestThreatIndex = 0;
    g_landingTimer = kUpdateFrameLandingMessageTimer;
    g_planeTable.planes[0].mapX = kUpdateFrameLandingTargetX;
    g_planeTable.planes[0].mapY = kUpdateFrameLandingTargetY;
    g_planeTable.planes[0].flags = kUpdateFrameLandingFlags;
    updateFrame();
    require(g_landingTimer == kUpdateFrameLandingTimerAfterResupply &&
                std::strcmp(g_lastHudMessage, "Ready for takeoff") == 0,
            "updateFrame preserves original non-auto landing resupply message on frameTick bit 3 set");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    updateGame.unk4 = 1;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameTick;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_savedPosVisible = 1;
    g_altitude = kUpdateFrameSavedAltitude;
    updateFrame();
    require(g_missionEndedFlag[0] == 1 &&
                updateComm.landingType == kLandingCrashed,
            "updateFrame preserves the saved-position visible eject/crash finalization path");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameTick;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_savedPosVisible = 1;
    g_altitude = kUpdateFrameSavedAltitude;
    g_autopilotAltitude = kUpdateFrameSavedAltitude;
    updateFrame();
    require(g_altitude == kUpdateFrameRecoveredAltitude &&
                g_autopilotAltitude == 0 &&
                g_missionEndedFlag[0] == 0,
            "updateFrame bumps altitude by the original saved-position recovery amount when no crash is pending");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameScale - 1;
    g_missionTick = kUpdateFrameMissionEventTick;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    updateFrame();
    require(g_missionTick == kUpdateFrameMissionEventTick + 1 &&
                g_eventLogCount == kExpectedOneCall &&
                g_replayLog.events[0].type == kUpdateFrameMissionEventType,
            "updateFrame appends the original periodic mission replay event every 32 mission ticks");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    frameTick = kUpdateFrameScale - 1;
    g_missionTick = kUpdateFrameRadioMissionTick;
    g_autopilotEngaged = 1;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    // glibc rand seed 4 drives randomRange(3) to the original Strike Eagle radio branch.
    std::srand(4);
    updateFrame();
    require(g_missionTick == kUpdateFrameRadioMissionTickAfterUpdate &&
                keyValue == kRadioStrikeEagleKey &&
                std::strcmp(g_lastHudMessage, "F15 Strike Eagle") == 0,
            "updateFrame emits the original autopilot radio cue every fourth mission tick");

    resetGameplayState();
    updateGame = {};
    updateComm = {};
    gameData = &updateGame;
    commData = &updateComm;
    g_initPhase = kUpdateFrameInitPhaseSteady;
    g_frameRateScaling = kUpdateFrameScale;
    g_frameRateAccum = kUpdateFrameScale * 4 - 1;
    frameTick = kUpdateFrameTick;
    g_ViewX = kUpdateFrameWorldX;
    g_ViewY = kUpdateFrameWorldY;
    g_viewZ = kUpdateFrameViewZ;
    g_groundAltitude = 0;
    g_nearestThreatRange = kUpdateFrameNoThreatRange;
    g_bulletTrackCount = kUpdateFrameBulletTrackCount;
    g_groundUnitCount = 1;
    g_simObjects[0].damage = kUpdateFrameGroundAlertDamage;
    g_simObjects[0].flags.b[0] |= 2;
    updateFrame();
    require(g_enemyAlertFlag == kExpectedOneCall,
            "updateFrame counts damaged active ground sim objects in the original periodic enemy-alert scan");

	    resetGameplayState();
    g_smokeSourceIdx = kTracerSmokeSource;
    g_planeTable.planes[kTracerSmokeSource].mapX = 123;
    g_planeTable.planes[kTracerSmokeSource].mapY = 234;
    frameTick = kTracerFrameTick;
    g_particles[0].alt = 0x200;
    g_particles[0].posY = 10;
    g_particles[0].spin = 0;
    updateTracerParticles();
    require(g_particles[0].alt == 0x200 + 10 &&
                g_particles[0].posY == 10 + ((0x200 + 10) >> 9) &&
                reinterpret_cast<unsigned char *>(&g_particles[0].spin)[1] == kParticleSpinHighByteStep,
            "updateTracerParticles rises existing smoke particles and advances spin high byte");
    require(g_particles[kTracerSpawnSlot].posX == 123 &&
                g_particles[kTracerSpawnSlot].posY == 234 &&
                g_particles[kTracerSpawnSlot].alt == kTracerAltReset &&
                g_smokeParticleSlot == kTracerSpawnSlot,
            "updateTracerParticles reseeds the original smoke ring slot every 16 frames");

    resetGameplayState();
    struct Game frameGame = {};
    gameData = &frameGame;
    g_inputDisabled = 1;
    g_rngSeed = 1;
    g_targetSlots[0].seedNoise = kFrameRandomSeedNoiseA;
    g_targetSlots[1].seedNoise = kFrameRandomSeedNoiseB;
    g_dacSupported = 0;
    frameGame.theater = 0;
    initFrameRandom();
    require(g_clearStatusPanelCalls == 1 &&
                (frameTick & ~kFrameRandomMask) == 0 &&
                g_unusedFrameVal == kFrameRandomUnusedVal &&
                g_missionTick == 0 &&
                g_setupDacCalls == 0,
            "initFrameRandom seeds frame tick, clears status, derives unused seed nibble, and resets mission tick");

    resetGameplayState();
    frameGame = {};
    gameData = &frameGame;
    g_inputDisabled = 1;
    g_rngSeed = kFrameRandomNightSeed;
    g_dacSupported = 1;
    frameGame.theater = 0;
    initFrameRandom();
    require(g_nightMode != 0 &&
                g_setupDacCalls == kExpectedOneCall,
            "initFrameRandom invokes the original DAC refresh when seeded into supported night mode");
    g_nightMode = 0;
    g_dacSupported = 0;

    resetGameplayState();
    size3d3 = kWaypointFeatureDynamicNameIndex;
    g_targetSlots[0].flags = kTargetDynamicNameFlag;
    g_targetSlots[0].planeIndex = 2;
    g_planeTable.planes[2].mapX = 100;
    g_planeTable.planes[2].mapY = 200;
    g_stubNearestTile.id = kWaypointFeatureShapeId;
    g_shapeTargetCategory[kWaypointFeatureShapeId] = 7;
    g_targetNameTable[kWaypointFeatureShapeId] = const_cast<char *>("Bridge");
    g_targetNameTable[kWaypointFeatureDynamicNameIndex] = g_stringPool;
    buf3d3[kWaypointFeatureDynamicNameIndex] = kWaypointFeatureShapeOffset;
    findWaypointFeatures();
    require(g_lastNearestWorldX == (static_cast<uint32>(100) << 5) &&
                g_lastNearestWorldY == ((0x8000UL - 200UL) << 5) &&
                g_shapeTargetCategory[kWaypointFeatureDynamicNameIndex] == 7 &&
                std::strcmp(g_targetNameTable[kWaypointFeatureDynamicNameIndex], "Bridge") == 0 &&
                g_planeTable.planes[2].nameIndex == kWaypointFeatureDynamicNameIndex + 0x100 &&
                g_addTileEntryCalls == 1 &&
                g_lastTileEntryValue == kWaypointFeatureShapeOffset &&
                g_lastTileEntryShape == kWaypointFeatureDynamicNameIndex,
            "findWaypointFeatures copies nearest tile metadata into the dynamic target-name slot");

    resetGameplayState();
    commData = &frameComm;
    g_landTargetId[0] = 1;
    g_waterTargetId[0] = 2;
    g_planeCount = 1;
    g_targetEntityCount = 1;
    g_planeScanCount = 1;
    g_groundUnitCount = 1;
    g_simObjects[0].objType = 9;
    g_shapeTargetCategory[0] = 3;
    g_tileKillTally[0] = 4;
    std::strcpy(g_stringPool, "A");
    waypoints[0].mapX = 11;
    g_targetSlots[0].state = 5;
    g_eventLogCount = 0;
    moveDataFar();
    require(flagFarToNear == 0 &&
                farPointer == frameComm.worldBuf + 1 + 1 + 2 + 2 + 2 + g_planeCount * 16 +
                                  2 + g_groundUnitCount * 36 + 100 + 100 + 750 +
                                  0x100 + 2 + 2 + 16 + 36 + 1536,
            "moveDataFar writes the original world block layout followed by replay events");

    resetGameplayState();
    struct Game missionGame = {};
    struct GameComm missionComm = {};
    commData = &missionComm;
    gameData = &missionGame;
    g_planeCount = kMissionStringPlaneIndex + 1;
    g_targetEntityCount = g_planeCount;
    g_planeScanCount = g_planeCount;
    g_planeTable.planes[kMissionStringPlaneIndex].mapX = kMissionStringPlaneX;
    g_planeTable.planes[kMissionStringPlaneIndex].mapY = kMissionStringPlaneY;
    g_targetSlots[0].viewIndex = kMissionStringPlaneIndex;
    waypoints[0].mapX = kMissionStringWaypointX;
    waypoints[0].mapY = kMissionStringWaypointY;
    // Packed original string table: initMissionStrings discovers entries by
    // walking NUL terminators in this 750-byte pool.
    const char missionNames[] = "Alpha\0Bravo\0Charlie";
    std::memcpy(g_stringPool, missionNames, sizeof(missionNames));
    moveDataFar();
    std::memset(&g_planeTable, 0, sizeof(g_planeTable));
    std::memset(waypoints, 0, sizeof(struct Waypoint) * kMissionWaypointCount);
    std::memset(g_targetSlots, 0, sizeof(struct TargetSlot) * kMissionTargetSlotCount);
    std::memset(g_stringPool, 0, kMissionStringPoolSize);
    std::memset(g_targetNameTable, 0, sizeof(char *) * kMissionTargetNameCount);
    missionGame.difficulty = kMissionStringDifficultyCombat;
    initMissionStrings();
    require(std::strcmp(g_targetNameTable[0], "Alpha") == 0 &&
                std::strcmp(g_targetNameTable[1], "Bravo") == 0 &&
                std::strcmp(g_targetNameTable[2], "Charlie") == 0,
            "initMissionStrings rebuilds original target-name pointers from packed world strings");
    require(g_ViewX == (static_cast<int32>(kMissionStringPlaneX) << kWorldCoordShift) + 2 &&
                g_ViewY ==
                    ((kOriginalMapYOffset - static_cast<int32>(kMissionStringPlaneY))
                     << kWorldCoordShift) &&
                g_viewX_ == kMissionStringPlaneX &&
                g_viewY_ == kMissionStringPlaneY,
            "initMissionStrings starts combat missions at the original primary target view");
    missionGame.difficulty = kMissionStringDifficultyTraining;
    initMissionStrings();
    require(g_ViewX == (static_cast<int32>(kMissionStringWaypointX) << kWorldCoordShift) + 2 &&
                g_ViewY ==
                    ((kOriginalMapYOffset - static_cast<int32>(kMissionStringWaypointY))
                     << kWorldCoordShift) &&
                g_viewX_ == kMissionStringWaypointX &&
                g_viewY_ == kMissionStringWaypointY,
            "initMissionStrings starts training missions at waypoint zero");

    resetGameplayState();
    g_frameRateScaling = 20;
    g_slowMotionMode = 0;
    recalcTimeScale();
    require(g_frameSyncWait ==
                clampRange((-(kFrameSyncDividend / 20 - kFrameSyncBias)) >> 1,
                           kFrameSyncMin, kFrameSyncMax),
            "recalcTimeScale computes frame wait before clamping frame-rate scaling");
    require(g_frameRateScaling == 15 && g_bulletTrackCount == kBulletTrackMax &&
                g_threatTimerInit == kThreatTimerScale * 15 &&
                g_threatDisplayTtl == kThreatDisplayScale * 15,
            "recalcTimeScale preserves original clamped timing counters");

    resetGameplayState();
    g_frameRateScaling = 2;
    g_slowMotionMode = 2;
    recalcTimeScale();
    require(g_frameSyncWait == 0 && g_frameRateScaling == 2 &&
                g_bulletTrackCount == (g_frameRateScaling << 1),
            "recalcTimeScale allows mode-2 slow motion minimum before bullet-track scaling");

    resetGameplayState();
    g_slowMotionMode = 2;
    g_frameRateScaling = 6;
    exitSlowMotion();
    require(g_slowMotionMode == 1 && g_frameRateScaling == 12,
            "exitSlowMotion returns mode 2 to mode 1 and doubles the frame-rate scale");
    g_frameRateScaling = 9;
    exitSlowMotion();
    require(g_slowMotionMode == 1 && g_frameRateScaling == 9,
            "exitSlowMotion ignores calls when already out of mode 2");

    resetGameplayState();
    g_detailLevel = 3;
    g_lodDistBase = kLodDistanceBase;
    g_lodDistScale = kLodDistanceScale;
    std::memset(colorLut + 0x10, 0, kLodRecordCount * sizeof(int16));
    setupLodDistances();
    for (int lod = 0; lod < kLodRecordCount; ++lod) {
        const int16 expected =
            static_cast<int16>(kLodBaseDistance << (lod + kLodDetailCap));
        require(reinterpret_cast<int16 *>(colorLut + 0x10)[lod] == expected,
                "setupLodDistances writes original six LOD thresholds");
    }
    require(g_lodDistNear == kLodDistanceBase + kLodDistanceScale &&
                g_lodDistFar ==
                    clampRange(kLodDistanceScale << 1, kLodFarMin, kLodFarMax),
            "setupLodDistances preserves original near/far distance formula");
    require(*reinterpret_cast<int16 *>(colorLut + 0x20) ==
                (kLodDetailCap + 1) * kLodSummaryStep,
            "setupLodDistances caps detail level before writing the summary word");

    resetGameplayState();
    updateEngineSound();
    require(g_engineOnCalls == 1 && g_engineOffCalls == 0,
            "updateEngineSound enables engine drone when sound is active and not ejecting");
    g_axisInputAccum[2] = 1;
    updateEngineSound();
    require(g_engineOffCalls == 1,
            "updateEngineSound disables engine drone when sound level is muted");
    g_axisInputAccum[2] = 0;
    g_ejectState = 1;
    updateEngineSound();
    require(g_engineOffCalls == 2,
            "updateEngineSound disables engine drone during ejection");

    resetGameplayState();
    g_axisInputAccum[2] = 2;
    makeSound(17, 1);
    require(g_audioSoundCalls == 0 && g_engineOffCalls == 1,
            "makeSound suppresses sounds below current priority but still refreshes engine audio");
    makeSound(18, 2);
    require(g_audioSoundCalls == 1 && g_lastAudioSound == 18,
            "makeSound plays sounds whose priority reaches the original sound level gate");
    g_ejectState = 1;
    makeSound(19, 1);
    require(g_audioSoundCalls == 1,
            "makeSound suppresses low-priority sound effects during ejection");
    makeSound(20, 2);
    require(g_audioSoundCalls == 2 && g_lastAudioSound == 20,
            "makeSound allows high-priority sound effects during ejection");

    resetGameplayState();
    g_axisInputAccum[2] = 1;
    f15DgtlResult = voiceCueThresholds[kVoiceGunCue] + 1;
    playVoiceCue(kVoiceGunCue);
    require(g_audioSampleCalls == 1 && g_lastAudioSample == kVoiceGunCue,
            "playVoiceCue uses original sound-level and digital-threshold gates");
    g_axisInputAccum[2] = 2;
    playVoiceCue(kVoiceGunCue);
    require(g_audioSampleCalls == 1, "playVoiceCue is muted at original sound level 2");
    g_axisInputAccum[2] = 0;
    f15DgtlResult = voiceCueThresholds[kVoiceGunCue];
    playVoiceCue(kVoiceGunCue);
    require(g_audioSampleCalls == 1,
            "playVoiceCue requires the original strict greater-than threshold");
    f15DgtlResult = -1;
    playVoiceCue(kVoiceGunCue);
    require(g_audioSampleCalls == 2,
            "playVoiceCue keeps the original unsigned comparison for negative result words");

    resetGameplayState();
    struct GameComm keyComm = {};
    commData = &keyComm;
    g_fireCooldown = 2;
    keyDispatch(0);
    require(g_fireCooldown == 1 &&
                g_switchIndicatorCalls == 2 &&
                g_lastIndicatorIdx == 2 &&
                g_lastIndicatorColor == 2,
            "keyDispatch handles the original no-scan-code frame tail and indicator refresh");

    resetGameplayState();
    commData = &keyComm;
    keyDispatch(kKeyZoomIn);
    require(g_zoomInCalls == 1,
            "keyDispatch routes the original Z key to zoomIn");
    keyDispatch(kKeyZoomOut);
    require(g_zoomOutCalls == 1,
            "keyDispatch routes the original X key to zoomOut");

    resetGameplayState();
    commData = &keyComm;
    g_eventTimers[kCountermeasureChaff] = 1;
    keyDispatch(kKeyChaff);
    require(g_eventTimers[kCountermeasureChaff] == 0 &&
                std::strcmp(g_lastHudMessage, "Chaff released") == 0,
            "keyDispatch routes the original C key through chaff countermeasure release");

    resetGameplayState();
    commData = &keyComm;
    g_eventTimers[kCountermeasureFlare] = 1;
    keyDispatch(kKeyFlare);
    require(g_eventTimers[kCountermeasureFlare] == 0 &&
                std::strcmp(g_lastHudMessage, "Flare released") == 0,
            "keyDispatch routes the original F key through flare countermeasure release");

    resetGameplayState();
    commData = &keyComm;
    g_viewZ = 500;
    g_groundAltitude = 0;
    keyDispatch(kKeyLandingGear);
    require((g_playerPlaneFlags & 1) != 0 &&
                g_gearDownArmed == 0 &&
                g_lastAudioSound == kLandingGearSound,
            "keyDispatch toggles landing gear in flight and plays the original gear sound");

    resetGameplayState();
    commData = &keyComm;
    g_viewZ = g_groundAltitude;
    keyDispatch(kKeyLandingGear);
    require((g_playerPlaneFlags & 1) == 0 &&
                g_audioSoundCalls == 0,
            "keyDispatch preserves original landing-gear no-op on the ground");

    resetGameplayState();
    commData = &keyComm;
    allocSize = 12345;
    keyDispatch(kKeyMemoryAvailable);
    require(std::strcmp(g_lastHudMessage, "Memory Available:12345") == 0,
            "keyDispatch reports original memory-available text");

    resetGameplayState();
    commData = &keyComm;
    g_jiffiesPerFrame = 7;
    keyDispatch(kKeyJiffiesFrame);
    require(std::strcmp(g_lastHudMessage, "Jiffies/Frame 7") == 0,
            "keyDispatch reports original jiffies-per-frame text");

    resetGameplayState();
    commData = &keyComm;
    g_slowMotionMode = 1;
    g_frameRateScaling = 12;
    keyDispatch(kKeySlowMotion);
    require(g_slowMotionMode == 2 &&
                g_frameRateScaling == 6 &&
                g_bulletTrackCount == 12,
            "keyDispatch advances original slow-motion mode 1 to mode 2 and recalculates timing");

    resetGameplayState();
    commData = &keyComm;
    g_slowMotionMode = 2;
    g_frameRateScaling = kSlowMotionExitScaleStart;
    keyDispatch(kKeySlowMotion);
    require(g_slowMotionMode == 1 &&
                g_frameRateScaling == kSlowMotionExitScaleAfter,
            "keyDispatch exits original mode-2 slow motion through exitSlowMotion");

    resetGameplayState();
    commData = &keyComm;
    g_axisInputAccum[2] = 3;
    keyDispatch(kKeySoundLevel);
    require(g_axisInputAccum[2] == 0 &&
                std::strcmp(g_lastHudMessage, "Sounds 3") == 0 &&
                g_engineOnCalls == 1,
            "keyDispatch wraps the original sound level and refreshes engine audio");

    resetGameplayState();
    commData = &keyComm;
    g_dacSupported = 1;
    keyDispatch(kKeyNightMode);
    require((g_nightMode & 0xff) == 1 &&
                g_setupDacCalls == 1,
            "keyDispatch toggles original night mode and refreshes DAC when supported");

    resetGameplayState();
    commData = &keyComm;
    keyDispatch(kKeyTacticalMap);
    require((g_playerPlaneFlags & kTacticalMapFlag) != 0 &&
                (reinterpret_cast<uint8 *>(&keyComm)[kCommSetupTacticalFlagOffset] & 1) != 0,
            "keyDispatch toggles the original tactical-map flag and mirrors it into COMM");

    resetGameplayState();
    commData = &keyComm;
    g_currentWeaponType = 2;
    g_lockedTargetKilled = 1;
    missleSpec[0].weaponIdx = 0;
    missleSpec[0].ammo = 1;
    keyDispatch(kKeySidewinder);
    require(missileSpecIndex == 0 &&
                g_currentWeaponType == 1 &&
                g_lockedTargetKilled == 0 &&
                std::strcmp(g_lastHudMessage, "Sidewinder armed") == 0,
            "keyDispatch selects original Sidewinder/AAM weapon mode and clears stale lock-kill state");

    resetGameplayState();
    commData = &keyComm;
    missleSpec[1].weaponIdx = 1;
    missleSpec[1].ammo = 1;
    keyDispatch(kKeyAmraam);
    require(missileSpecIndex == 1 &&
                g_currentWeaponType == 1 &&
                std::strcmp(g_lastHudMessage, "AMRAAM  armed") == 0,
            "keyDispatch selects original AMRAAM weapon slot");

    resetGameplayState();
    commData = &keyComm;
    g_currentWeaponType = 1;
    g_lockedTargetKilled = 1;
    missleSpec[2].weaponIdx = 5;
    missleSpec[2].ammo = 1;
    keyDispatch(kKeyMaverick);
    require(missileSpecIndex == 2 &&
                g_currentWeaponType == 2 &&
                g_lockedTargetKilled == 0 &&
                std::strcmp(g_lastHudMessage, "Maverick armed") == 0,
            "keyDispatch selects original Maverick/ground weapon mode and clears stale lock-kill state");

    resetGameplayState();
    commData = &keyComm;
    keyDispatch(kKeyCtrlY);
    require(regs.h.ah == kTextBlinkBiosFunction &&
                regs.h.al == kTextBlinkDisableMode,
            "keyDispatch Ctrl-Y disables text blink through the original BIOS register values");

    resetGameplayState();
    commData = &keyComm;
    g_radarScopeRange = kRadarLongRange;
    keyDispatch(kKeyRadarRange);
    require(g_radarScopeRange == kRadarMediumRange &&
                std::strcmp(g_lastHudMessage, "Medium range radar") == 0,
            "keyDispatch cycles radar scope to the original medium-range message");
    keyDispatch(kKeyRadarRange);
    require(g_radarScopeRange == kRadarShortRange &&
                std::strcmp(g_lastHudMessage, "Short range radar") == 0,
            "keyDispatch cycles radar scope to the original short-range message");
    keyDispatch(kKeyRadarRange);
    require(g_radarScopeRange == kRadarLongRange &&
                std::strcmp(g_lastHudMessage, "Long range radar") == 0,
            "keyDispatch wraps radar scope to the original long-range message");

    resetGameplayState();
    commData = &keyComm;
    g_detailLevel = 0;
    keyDispatch(kKeyDetailLevel);
    require(g_detailLevel == kDetailWrappedValue &&
                std::strcmp(g_lastHudMessage, "Detail Level 3") == 0 &&
                g_lodDistNear == g_lodDistBase + g_lodDistScale,
            "keyDispatch wraps detail level through original modecode gate and refreshes LOD distances");

    resetGameplayState();
    commData = &keyComm;
    g_kbdSensitivity = 2;
    keyDispatch(kKeySensitivity);
    require(g_kbdSensitivity == kSensitivityWrappedValue &&
                std::strcmp(g_lastHudMessage, "Kybd Sensitivity1") == 0,
            "keyDispatch wraps keyboard sensitivity and emits the original one-based message");

    resetGameplayState();
    commData = &keyComm;
    keyDispatch(kKeyDirector);
    require(g_directorMode == 1 &&
                std::strcmp(g_lastHudMessage, "Director 1") == 0,
            "keyDispatch advances director mode from off to original mode 1 message");
    keyDispatch(kKeyDirector);
    keyDispatch(kKeyDirector);
    require(g_directorMode == 0 &&
                std::strcmp(g_lastHudMessage, "Director off") == 0,
            "keyDispatch wraps director mode back to off");

    resetGameplayState();
    commData = &keyComm;
    waypointIndex = 0;
    keyDispatch(kKeyWaypoint);
    require(waypointIndex == 1 &&
                std::strcmp(g_lastHudMessage, "Waypoint: Primary Target") == 0,
            "keyDispatch advances waypoint to the original primary-target cue");
    keyDispatch(kKeyWaypoint);
    require(waypointIndex == 2 &&
                std::strcmp(g_lastHudMessage, "Waypoint: Secondary Target") == 0,
            "keyDispatch advances waypoint to the original secondary-target cue");
    g_closestThreatIndex = 5;
    keyDispatch(kKeyWaypoint);
    require(waypointIndex == 3 &&
                g_targetSlots[1].viewIndex == g_closestThreatIndex &&
                std::strcmp(g_lastHudMessage, "Waypoint: Friendly Airbase") == 0,
            "keyDispatch advances waypoint to the original friendly-airbase cue and target slot");
    keyDispatch(kKeyWaypoint);
    require(waypointIndex == 1,
            "keyDispatch wraps waypoint index to the original primary-target entry");

    resetGameplayState();
    commData = &keyComm;
    g_viewZ = 500;
    keyDispatch(kKeyAutopilot);
    require(g_autopilotAltitude == kAutopilotMinimumAlt &&
                std::strcmp(g_lastHudMessage, "Autopilot on") == 0,
            "keyDispatch enables autopilot at the original minimum altitude");
    keyDispatch(kKeyAutopilot);
    require(g_autopilotAltitude == 0 &&
                std::strcmp(g_lastHudMessage, "Autopilot off") == 0,
            "keyDispatch disables autopilot on a repeated autopilot key");

    resetGameplayState();
    commData = &keyComm;
    g_fireCooldown = kFireCooldownStart;
    keyDispatch(kKeyGroundTargetLock);
    require(g_fireCooldown == kFireCooldownAfterDispatch,
            "keyDispatch decrements the original fire cooldown at the dispatch tail");

    resetGameplayState();
    commData = &keyComm;
    g_groundTargetLock = 0;
    keyDispatch(kKeyGroundTargetLock);
    require((g_groundTargetLock & 0x80) != 0,
            "keyDispatch marks the original ground-target lock byte invalid on T");

    resetGameplayState();
    commData = &keyComm;
    keyDispatch(kKeyBackspaceFire);
    require(g_axisInputAccum[0] == 1,
            "keyDispatch maps Backspace to the original secondary fire-axis accumulator");
    keyDispatch(kKeyEnterFire);
    require(g_axisInputAccum[1] == 1 &&
                g_fireCooldown == 4,
            "keyDispatch maps Enter to the original fire-axis accumulator and fire cooldown");
    keyValue = 0x55;
    keyDispatch(kKeySpaceForwardView);
    require(keyValue == 0,
            "keyDispatch maps Space to the original forward cockpit view");
    keyDispatch(kKeyFunction1);
    require(keyValue == kFunction1KeyValue,
            "keyDispatch maps F1 to the original front-view keyValue");
    keyDispatch(kKeyFunction2);
    require(keyValue == kFunction2KeyValue,
            "keyDispatch maps F2 to the original left-view keyValue");
    keyDispatch(kKeyFunction3);
    require(keyValue == kFunction3KeyValue,
            "keyDispatch maps F3 to the original right-view keyValue");
    keyDispatch(kKeyFunction4);
    require(keyValue == kFunction4KeyValue,
            "keyDispatch maps F4 to the original rear-view keyValue");
    keyDispatch(kKeyFunction5);
    require(keyValue == kFunction5KeyValue,
            "keyDispatch maps F5 to the original chase-view keyValue");
    keyDispatch(kKeyFunction6);
    require(keyValue == kFunction6KeyValue,
            "keyDispatch maps F6 to the original trailing-camera keyValue");
    keyDispatch(kKeyFunction7);
    require(keyValue == kFunction7KeyValue,
            "keyDispatch maps F7 to the original side chase keyValue");
    keyDispatch(kKeyFunction8);
    require(keyValue == kFunction8KeyValue,
            "keyDispatch maps F8 to the original director/missile camera keyValue");
    keyDispatch(kKeyFunction9);
    require(keyValue == kFunction9KeyValue,
            "keyDispatch maps F9 to the original target camera keyValue");
    keyDispatch(kKeyFunction10);
    require(keyValue == kFunction10KeyValue,
            "keyDispatch maps F10 to the original tracking camera keyValue");

    resetGameplayState();
    commData = &keyComm;
    g_ourRoll = 0;
    g_ourPitch = 0;
    g_knots = 0;
    g_viewX_ = 12;
    g_viewY_ = 34;
    g_viewZ = 56;
    std::srand(1); // Keeps the original bailout-survived branch deterministic.
    keyDispatch(kKeyEscapeEject);
    require(g_ejectState == 1 &&
                keyValue == kEjectCameraKeyValue &&
                g_lastAudioSound == kEjectSecondSound &&
                keyComm.bailoutSurvived == kEjectBailoutSurvived &&
                g_crashCamX == g_viewX_ &&
                g_crashCamY == g_viewY_ &&
                g_crashCamZ == g_viewZ + 8,
            "keyDispatch starts original eject sequence, records crash camera, and forces eject camera view");

    resetGameplayState();
    commData = &keyComm;
    g_playerPlaneFlags = kTacticalMapFlag;
    keyComm.weaponType[0] = 4;
    keyComm.weaponCount[0] = 5;
    keyDispatch(kKeyDebugReloadWeapons);
    require(missleSpec[0].weaponIdx == keyComm.weaponType[0] &&
                missleSpec[0].ammo == keyComm.weaponCount[0],
            "keyDispatch runs original debug reload-weapons command while tactical map flag is set");

    resetGameplayState();
    commData = &keyComm;
    g_playerPlaneFlags = kTacticalMapFlag;
    g_mapZoomLevel = kMapPanZoom;
    g_ViewX = 0;
    g_ViewY = 0;
    keyDispatch(kKeyMapPanDown);
    require(g_ViewY == (kMapPanStep >> kMapPanZoom),
            "keyDispatch pans tactical map down by the original zoom-scaled step");
    keyDispatch(kKeyMapPanUp);
    require(g_ViewY == 0,
            "keyDispatch pans tactical map up by the original zoom-scaled step");
    keyDispatch(kKeyMapPanLeft);
    require(g_ViewX == -(kMapPanStep >> kMapPanZoom),
            "keyDispatch pans tactical map left by the original zoom-scaled step");
    keyDispatch(kKeyMapPanRight);
    require(g_ViewX == 0,
            "keyDispatch pans tactical map right by the original zoom-scaled step");

    resetGameplayState();
    commData = &keyComm;
    g_ourRoll = 0x7000;
    g_ourPitch = 0x7000;
    g_knots = 2000;
    g_viewX_ = 98;
    g_viewY_ = 76;
    g_viewZ = 54;
    std::srand(1); // High attitude/speed forces the original finalizeMission eject failure branch.
    keyDispatch(kKeyEscapeEject);
    require(g_missionEndedFlag[0] == 1 &&
                keyComm.landingType == kLandingCrashed &&
                keyComm.bailoutSurvived == kEjectCrashOutcome &&
                g_ejectState == 1 &&
                keyValue == kEjectCameraKeyValue,
            "keyDispatch preserves original eject crash outcome for unsafe attitude and speed");

    resetGameplayState();
    commData = &keyComm;
    keyScancode = kKeyFunction1;
    dispatchKeyScancode();
    require(keyValue == kFunction1KeyValue,
            "dispatchKeyScancode forwards the latched BIOS scancode to keyDispatch");

    resetGameplayState();
    g_directorEventDeadline = kRadioPendingDeadline;
    keyValue = 0x55;
    g_viewTargetObj = 0x66;
    generateRandomRadioMessage();
    require(g_directorEventDeadline == kRadioPendingDeadline &&
                keyValue == 0x55 &&
                g_viewTargetObj == 0x66 &&
                g_autopilotAltitude == 0,
            "generateRandomRadioMessage preserves pending director events unchanged");

    resetGameplayState();
    g_planeCount = kRadioPlaneCount;
    g_targetNameTable[0] = const_cast<char *>("");
    g_targetNameTable[kRadioAircraftNameIdx] = const_cast<char *>("Bandit");
    g_planeTable.planes[kRadioAircraftIdxFromSeed].nameIndex = kRadioAircraftNameIdx;
    // glibc rand seed 2 drives randomRange(3) to the original aircraft branch,
    // then randomRange(g_planeCount - 3) selects aircraft index 5.
    std::srand(2);
    generateRandomRadioMessage();
    require(g_autopilotAltitude == kRadioAutopilotAltitude &&
                g_directorMode == kRadioDirectorMode &&
                g_viewTargetObj == kRadioViewTargetPlaneBase + kRadioAircraftIdxFromSeed &&
                keyValue == kRadioDirectorKey &&
                std::strcmp(g_lastHudMessage, "Bandit") == 0,
            "generateRandomRadioMessage selects an aircraft target and copies its original target name");

    resetGameplayState();
    g_groundUnitCount = kRadioGroundUnitCount;
    g_simObjects[kRadioGroundIdxFromSeed].speed = 250;
    g_simObjects[kRadioGroundIdxFromSeed].spec = 0;
    // glibc rand seed 1 drives randomRange(3) to the ground-patrol branch,
    // then randomRange(g_groundUnitCount) selects ground object index 1.
    std::srand(1);
    generateRandomRadioMessage();
    require(g_viewTargetObj == kRadioViewTargetGroundBase + kRadioGroundIdxFromSeed &&
                keyValue == kRadioDirectorKey &&
                std::strcmp(g_lastHudMessage, "MIG-23 on patrol") == 0,
            "generateRandomRadioMessage selects a moving ground object and emits the original patrol text");

    resetGameplayState();
    // glibc rand seed 4 drives randomRange(3) to the Strike Eagle branch.
    std::srand(4);
    generateRandomRadioMessage();
    require(keyValue == kRadioStrikeEagleKey &&
                std::strcmp(g_lastHudMessage, "F15 Strike Eagle") == 0,
            "generateRandomRadioMessage emits the original player-aircraft director cue");

    resetGameplayState();
    missileSpecIndex = 1;
    missleSpec[1].weaponIdx = 1;
    missleSpec[1].ammo = 0;
    selectMissile();
    require(std::strcmp(strBuf, "AMRAAM  not available") == 0 &&
                std::strcmp(g_lastHudMessage, strBuf) == 0,
            "selectMissile preserves original missile name and availability message");
    g_hudVisible = 1;
    g_pageFront = reinterpret_cast<int16 *>(std::calloc(4, sizeof(int16)));
    require(g_pageFront != nullptr, "test allocation for HUD marker page succeeds");
    missileSpecIndex = 2;
    missleSpec[2].weaponIdx = 5;
    missleSpec[2].ammo = 6;
    selectMissile();
    require(std::strcmp(strBuf, "Maverick armed") == 0 && g_weaponMarkerSel == 2 &&
                g_lineDrawCalls == 4,
            "selectMissile updates marker through original four-line HUD redraw path");
    std::free(g_pageFront);
    g_pageFront = nullptr;

    resetGameplayState();
    g_northSouthSign = kSpawnNorthSouthSign;
    g_padlockAircraft = -1;
    g_groundUnitCount = kSpawnGroundUnitCount;
    g_simObjects[kSpawnSlot].spec = kSpawnAircraftSpec;
    g_planeTable.planes[kSpawnTargetIdx].mapX = kSpawnMapX;
    g_planeTable.planes[kSpawnTargetIdx].mapY = kSpawnMapY;
    g_planeTable.planes[kSpawnTargetIdx].nameIndex = 2;
    g_planeTable.planes[kSpawnTargetIdx - 1].secondaryNameIndex = 0;
    g_targetNameTable[0] = const_cast<char *>("");
    g_targetNameTable[2] = const_cast<char *>("Spawn Base");
    spawnEnemyAircraft(kSpawnSlot, kSpawnTargetIdx);
    require(g_simObjects[kSpawnSlot].posX == kSpawnMapX &&
                g_simObjects[kSpawnSlot].posY == kSpawnMapY + kSpawnNormalYOffset &&
                g_simObjects[kSpawnSlot].alt == kSpawnNormalAltitude &&
                g_simObjects[kSpawnSlot].speed == kSpawnNormalSpeed,
            "spawnEnemyAircraft uses the original normal runway spawn offsets");
    require(g_simObjects[kSpawnSlot].worldX == (kSpawnMapX << kSpawnWorldShift) &&
                g_simObjects[kSpawnSlot].worldY ==
                    ((kSpawnMapY + kSpawnNormalYOffset) << kSpawnWorldShift) &&
                g_simObjects[kSpawnSlot].heading.w == 0 &&
                g_simObjects[kSpawnSlot].flags.w == kSpawnFlags &&
                g_simObjects[kSpawnSlot].objType == kSpawnTargetIdx,
            "spawnEnemyAircraft initializes original world position, heading, flags, and object type");
    require(g_simObjects[kSpawnSlot].timer ==
                static_cast<int16>(((long)aircraftTypes[kSpawnAircraftSpec].range << 11) *
                                   (long)g_frameRateScaling /
                                   (long)aircraftTypes[kSpawnAircraftSpec].maxSpeed) &&
                g_simObjects[kSpawnSlot].terrainColor == kSpawnTerrainColor &&
                g_readMapPixelCalls == kExpectedOneCall,
            "spawnEnemyAircraft preserves original timer and terrain-color setup");
    require(g_lastReadMapX == kSpawnMapX && g_lastReadMapY == kSpawnMapY,
            "spawnEnemyAircraft reads terrain color at the original target map coordinates");
    require(std::strcmp(g_lastHudMessage, "Spawn Base - MIG-23 taking off") == 0,
            "spawnEnemyAircraft emits the original takeoff message for visible ground-unit slots");

    resetGameplayState();
    g_inLandingCorridor = kFireLandingClear;
    missileSpecIndex = kFireAmraamLoadout;
    missleSpec[kFireAmraamLoadout].weaponIdx = kFireAmraamWeapon;
    missleSpec[kFireAmraamLoadout].ammo = kFireAmmoEmpty;
    fireMissile();
    require(std::strcmp(g_lastTimedMessage, "AMRAAM :0") == 0 &&
                g_timedMessageCalls == kExpectedOneCall &&
                missleSpec[kFireAmraamLoadout].ammo == kFireAmmoEmpty,
            "fireMissile preserves original empty-ammo message and leaves ammo unchanged");

    resetGameplayState();
    g_inLandingCorridor = kFireLandingClear;
    missileSpecIndex = kFireGunLoadout;
    missleSpec[kFireGunLoadout].weaponIdx = kFireGunWeapon;
    missleSpec[kFireGunLoadout].ammo = kFireAmmoStart;
    fireMissile();
    require(missleSpec[kFireGunLoadout].ammo == kFireAmmoStart &&
                g_lastMissileSlot == 0 && g_timedMessageCalls == 0,
            "fireMissile rejects spec-0 weapons before decrementing ammo or showing HUD text");

    resetGameplayState();
    g_inLandingCorridor = kFireLandingClear;
    g_ourRoll = kFireRollAbort;
    missileSpecIndex = kFireSidewinderLoadout;
    missleSpec[kFireSidewinderLoadout].weaponIdx = kFireSidewinderWeapon;
    missleSpec[kFireSidewinderLoadout].ammo = kFireAmmoStart;
    fireMissile();
    require(missleSpec[kFireSidewinderLoadout].ammo == kFireAmmoStart &&
                g_lastMissileSlot == 0,
            "fireMissile aborts at the original roll limit before consuming ammo");

    resetGameplayState();
    g_inLandingCorridor = kFireLandingClear;
    g_activePanelMode = kFirePanelMode;
    g_directorMode = kFireDirectorMode;
    g_directorEventDeadline = -1;
    frameTick = kFireFrameTick;
    g_frameRateScaling = kFireFrameRateScaling;
    g_viewX_ = kFireViewX;
    g_viewY_ = kFireViewY;
    g_viewZ = kFireViewZ;
    g_velocity = kFireVelocity;
    g_ourHead = kFireHeading;
    g_ourPitch = kFirePitch;
    g_ourRoll = kFireRoll;
    missileSpecIndex = kFireSidewinderLoadout;
    missleSpec[kFireSidewinderLoadout].weaponIdx = kFireSidewinderWeapon;
    missleSpec[kFireSidewinderLoadout].ammo = kFireAmmoStart;
    for (int slot = kPlayerMissileFirstSlot; slot < kPlayerMissileLastSlot; ++slot) {
        g_projectiles[slot].ttl = kFireBusyTtl;
    }
    const int fireSpec = missiles[kFireSidewinderWeapon].specIndex;
    const int expectedFireTtl =
        static_cast<int>(((static_cast<long>(sams[fireSpec].lockRange)
                           << (kFireTtlShiftBase -
                               (sams[fireSpec].weaponClass == kFireTtlGroundWeaponClass
                                    ? kFireTtlGroundShift
                                    : kFireTtlDefaultShift))) *
                          static_cast<long>(g_frameRateScaling)) /
                         static_cast<long>((sams[fireSpec].maxSpeed >> kFireTtlSpeedShift) +
                                           kFireTtlSpeedBias)) +
        kFireTtlBias;
    fireMissile();
    require(missleSpec[kFireSidewinderLoadout].ammo == kFireAmmoAfterShot &&
                g_lastMissileSlot == kFireChosenSlot,
            "fireMissile decrements ammo and claims the last free original player missile slot");
    require(g_projectiles[kFireChosenSlot].mapX == kFireViewX &&
                g_projectiles[kFireChosenSlot].mapY == kFireViewY &&
                g_projectiles[kFireChosenSlot].alt == kFireViewZ - kFireAltitudeBias &&
                g_projectiles[kFireChosenSlot].speed == (kFireVelocity >> kFireSpeedShift),
            "fireMissile initializes original player missile position, altitude, and speed");
    require(g_projectiles[kFireChosenSlot].worldX == kFireHeading &&
                g_projectiles[kFireChosenSlot].worldY == kFirePitch - kFirePitchBias &&
                g_projectiles[kFireChosenSlot].worldZ == kFireRoll &&
                g_projectiles[kFireChosenSlot].ttl == expectedFireTtl,
            "fireMissile preserves original heading, pitch bias, roll, and ttl formula");
    require(g_projectiles[kFireChosenSlot].specIdx == fireSpec &&
                g_projectiles[kFireChosenSlot].weaponIdx == kFireSidewinderWeapon &&
                g_projectiles[kFireChosenSlot].targetLock == -1,
            "fireMissile stores original projectile spec, weapon, and no-lock sentinel");
    require(std::strcmp(g_lastHudMessage, "Sidewinder fired") == 0 &&
                g_lastAudioSound == kFireSoundWithLock,
            "fireMissile emits original fired message and lock-capable missile sound");
    require(g_refreshPanelCalls == kExpectedOneCall && g_lastRefreshPanel == kFirePanelId,
            "fireMissile refreshes the original active weapons panel after firing");
    require(g_viewTargetObj == kFireChosenSlot && keyValue == kDirectorKeyValue &&
                g_directorEventDeadline == frameTick + kFireDirectorDelay * g_frameRateScaling,
            "fireMissile schedules the original mode-1 director event for the fired missile");

    resetGameplayState();
    g_inLandingCorridor = kFireLandingClear;
    g_hudVisible = 1;
    g_directorMode = kFireDirectorMode;
    g_directorEventDeadline = -1;
    g_frameRateScaling = kFireFrameRateScaling;
    g_viewX_ = kFireViewX;
    g_viewY_ = kFireViewY;
    g_viewZ = kFireViewZ;
    g_velocity = kFireVelocity;
    g_ourHead = kFireHeading;
    g_ourPitch = kFirePitch;
    g_groundTargetLock = kFireGroundLockIdx;
    missileSpecIndex = kFireAmraamLoadout;
    missleSpec[kFireAmraamLoadout].weaponIdx = kFireMaverickWeapon;
    missleSpec[kFireAmraamLoadout].ammo = kFireAmmoStart;
    fireMissile();
    require(missleSpec[kFireAmraamLoadout].ammo == kFireAmmoAfterShot &&
                std::strcmp(g_lastTimedMessage, "Maverick:1") == 0 &&
                g_timedMessageCalls == kExpectedOneCall,
            "fireMissile redraws the original HUD ammo count and timed message when HUD is visible");
    require(g_projectiles[kFireChosenSlot].targetLock == kFireGroundLockIdx &&
                std::strcmp(g_lastHudMessage, "Maverick fired") == 0 &&
                g_lastAudioSound == kFireSoundWithLock,
            "fireMissile locks original Maverick shots to the selected ground target");

    resetGameplayState();
    g_inLandingCorridor = kFireLandingClear;
    g_frameRateScaling = kFireFrameRateScaling;
    g_viewZ = kFireViewZ;
    g_velocity = kFireVelocity;
    g_ourPitch = kFirePitch;
    missileSpecIndex = kFireGunLoadout;
    missleSpec[kFireGunLoadout].weaponIdx = kFireRetardedBombWeapon;
    missleSpec[kFireGunLoadout].ammo = kFireAmmoStart;
    fireMissile();
    require(g_projectiles[kFireChosenSlot].ttl == kFireFallbackTtl &&
                g_projectiles[kFireChosenSlot].worldY == static_cast<int16>(kFireRetardedBombPitch) &&
                g_projectiles[kFireChosenSlot].speed == kFireRetardedBombSpeed,
            "fireMissile preserves original retarded-bomb ttl fallback, pitch, and speed");
    require(g_lastAudioSound == kFireSoundNoLock &&
                std::strcmp(g_lastHudMessage, "Rockeye fired") == 0,
            "fireMissile uses the original no-lock sound for zero-range retarded bombs");

    resetGameplayState();
    g_inLandingCorridor = kFireLandingClear;
    g_frameRateScaling = kFireFrameRateScaling;
    g_viewZ = kFireViewZ;
    g_velocity = kFireVelocity;
    g_groundTargetLock = kFireGroundLockIdx;
    missileSpecIndex = kFireGunLoadout;
    missleSpec[kFireGunLoadout].weaponIdx = kFireFreefallBombWeapon;
    missleSpec[kFireGunLoadout].ammo = kFireAmmoStart;
    fireMissile();
    require(g_projectiles[kFireChosenSlot].targetRef == kFireLoftTargetRef &&
                g_loftTargetIdx == kFireGroundLockIdx,
            "fireMissile stores the original loft target and computed free-fall bomb reference angle");

    resetGameplayState();
    g_inLandingCorridor = kFireLandingClear;
    g_frameRateScaling = kFireFrameRateScaling;
    g_viewZ = kFireViewZ;
    g_velocity = kFireVelocity;
    g_groundTargetLock = kFireGroundLockIdx;
    g_planeTable.planes[kFireGroundLockIdx].flags = 8;
    missileSpecIndex = kFireGunLoadout;
    missleSpec[kFireGunLoadout].weaponIdx = kFireHarpoonWeapon;
    missleSpec[kFireGunLoadout].ammo = kFireAmmoStart;
    fireMissile();
    require(g_projectiles[kFireChosenSlot].targetLock == kFireGroundLockIdx,
            "fireMissile locks original Harpoon-class shots only onto flagged target ships");

    resetGameplayState();
    g_northSouthSign = kSpawnCarrierNorthSouthSign;
    g_padlockAircraft = -1;
    g_simObjects[kSpawnCarrierSlot].spec = kSpawnAircraftSpec;
    g_planeTable.planes[kSpawnCarrierTargetIdx].mapX = kSpawnMapX;
    g_planeTable.planes[kSpawnCarrierTargetIdx].mapY = kSpawnMapY;
    g_planeTable.planes[kSpawnCarrierTargetIdx].flags = kCarrierTargetFlag;
    spawnEnemyAircraft(kSpawnCarrierSlot, kSpawnCarrierTargetIdx);
    require(g_simObjects[kSpawnCarrierSlot].posX == kSpawnMapX + kSpawnCarrierNorthSouthSign * kSpawnCarrierXOffset &&
                g_simObjects[kSpawnCarrierSlot].posY ==
                    kSpawnMapY - kSpawnCarrierNorthSouthSign * kSpawnCarrierYOffset &&
                g_simObjects[kSpawnCarrierSlot].alt == kSpawnCarrierAltitude &&
                g_simObjects[kSpawnCarrierSlot].speed == kSpawnCarrierSpeed,
            "spawnEnemyAircraft uses the original carrier/air target spawn offsets");
    require(static_cast<uint8>(g_simObjects[kSpawnCarrierSlot].heading.b[1]) ==
                kSpawnCarrierHeadingHighByte,
            "spawnEnemyAircraft preserves the original carrier heading high-byte adjustment");
    require(g_lastReadMapX == kSpawnMapX && g_lastReadMapY == kSpawnMapY,
            "spawnEnemyAircraft carrier path reads terrain color at the original target map coordinates");

    resetGameplayState();
    g_enemyAirRemaining = 3;
    g_padlockAircraft = kDestroyedAircraftSlot;
    g_currentWeaponType = kMissileWeaponType;
    g_airTargetLock = kDestroyedAircraftSlot;
    g_simObjects[kDestroyedAircraftSlot].spec = kDestroyedAircraftSpec;
    g_simObjects[kDestroyedAircraftSlot].posX = kDestroyedAircraftX;
    g_simObjects[kDestroyedAircraftSlot].posY = kDestroyedAircraftY;
    g_simObjects[kDestroyedAircraftSlot].alt = kDestroyedAircraftAlt;
    g_simObjects[kDestroyedAircraftSlot].flags.w = kDestroyedAircraftFlags;
    g_simObjects[kDestroyedAircraftSlot].speed = 0;
    const int initialKillCount = aircraftTypes[kDestroyedAircraftSpec].killCount;
    destroyAircraft(kDestroyedAircraftSlot);
    require(aircraftTypes[kDestroyedAircraftSpec].killCount == initialKillCount + 1 &&
                g_enemyAirRemaining == 2 && g_padlockAircraft == -1,
            "destroyAircraft updates original kill count, enemy-air counter, and padlock target");
    require(g_simObjects[kDestroyedAircraftSlot].flags.w == kDestroyedStoppedFlags,
            "destroyAircraft applies the original stopped-aircraft flag mask after marking destruction");
    require(g_wreckX == kDestroyedAircraftX && g_wreckY == kDestroyedAircraftY &&
                g_wreckAlt == kDestroyedAircraftAlt &&
                g_wreckFallVel == kDestroyedWreckFallVelocity &&
                g_smokeSourceIdx == -1,
            "destroyAircraft seeds the original wreck state");
    require(g_eventLogCount == kExpectedOneCall &&
                g_replayLog.events[0].type == kDestroyedEventType &&
                g_replayLog.events[0].arg == kDestroyedEventArg,
            "destroyAircraft appends the original aircraft-destroyed replay event");
    require(std::strcmp(strBuf, aircraftTypes[kDestroyedAircraftSpec].name) == 0 &&
                g_lastAudioSound == kDestroyedSoundId &&
                g_lockedTargetKilled == kExpectedOneCall,
            "destroyAircraft writes the original HUD text, sound, and missile-lock kill flag");

    destroyAircraft(kDestroyedAircraftSlot);
    require(aircraftTypes[kDestroyedAircraftSpec].killCount == initialKillCount + 2 &&
                g_eventLogCount == 2 &&
                g_replayLog.events[1].arg == kDestroyedRepeatEventArg,
            "destroyAircraft preserves the original repeat-count quirk for stopped aircraft whose flag mask clears the destroyed bit");

    resetGameplayState();
    g_missionStatus = kBombMissionStatus;
    g_autopilotEngaged = 0;
    bombTarget();
    require(g_gunHits == kBombExpectedHits &&
                (g_bombDamageMask & ~kBombDamageBitMask) == 0 &&
                g_damageTakenFlag == kExpectedOneCall,
            "bombTarget applies the original inclusive mission-status damage loop");
    require(g_refreshPanelCalls == kExpectedOneCall && g_lastRefreshPanel == kBombPanelId &&
                g_lastAudioSound == 0,
            "bombTarget refreshes the original damage panel and plays the hit sound");

    resetGameplayState();
    g_playerPlaneFlags = kBombSuppressionFlag;
    g_autopilotEngaged = 0;
    bombTarget();
    require(g_gunHits == 0 && g_bombDamageMask == 0 &&
                g_refreshPanelCalls == 0 && g_damageTakenFlag == 0,
            "bombTarget is suppressed when the original player-plane flag is set");

    resetGameplayState();
    g_autopilotEngaged = -1;
    bombTarget();
    require(g_gunHits == 0 && g_refreshPanelCalls == 0,
            "bombTarget is suppressed when autopilot state is the original -1 sentinel");

    std::cout << "gameplay_behavior_tests passed\n";
    return 0;
}
