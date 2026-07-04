#include "eg3dview.h"
#include "egcode.h"
#include "egdata.h"
#include "egflight.h"
#include "egframe.h"
#include "egkeys.h"
#include "egmath.h"
#include "egpic.h"
#include "egtacmap.h"
#include "egthreat.h"
#include "egtypes.h"
#include "egui.h"
#include "offsets.h"
#include "log.h"
#include "gfx.h"
#include "r2d.h"
#include "slot.h"
#include "const.h"
#include "comm.h"
#include "eginput.h"
#include "input.h"

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private helpers for this translation unit. */
void stepFlightModel();
void applyRotationDelta(const int16 *matA, const int16 *matB);
void computeAttitudeAngles(void);
void rebuildOrientation();
uint16 signedRatio16(int16, int16);
int16 valueToAngle(int16 value);
int16 complementAngle(int16 value);
void renderFrame();
void drawVectorShape(const int16 *shapeData);
void waitForKeyPress(void);

void stepFlightModel(void) {
    // Local variables - names chosen to match MSC 5.1 hash-based stack layout
    // Within same hash bucket: first declared → highest BP offset (LIFO)
    // Dummies fill gaps to achieve sub sp, 0x3E (31 word slots)
    int16 p;                                // dummy:  bp-0x02 (bucket 0)
    int16 a, q;                             // dummies: bp-0x04, bp-0x06 (bucket 1)
    int16 prevAlt, aa, r;                   // var_C=prevAlt at bp-0x0c, dummies at bp-0x0a,bp-0x08 (bucket 2)
    int16 ab, tgtIdx, bearing;              // dummy=ab at bp-0x12, var_10=tgtIdx at bp-0x10, var_E=bearing at bp-0x0e (bucket 3)
    int16 yaw, yawAngle, tmpVal;            // var_18=yaw at bp-0x18, var_16=yawAngle at bp-0x16, var_14=tmpVal at bp-0x14 (bucket 4)
    int16 ad, u, targetVel;                 // dummies at bp-0x1e,bp-0x1c, var_1A=targetVel at bp-0x1a (bucket 5)
    int16 turbulence, horizVel, pitchAngle; // var_24=turbulence at bp-0x24, var_22=horizVel at bp-0x22, var_20=pitchAngle at bp-0x20 (bucket 6)
    int16 rollAngle, w;                     // var_28=rollAngle at bp-0x28, dummy=w at bp-0x26 (bucket 7)
    int16 headingErr, dx;                   // var_2C=headingErr at bp-0x2c, var_2A=dx at bp-0x2a (bucket 8)
    int16 i, y;                             // dummies: bp-0x2e, bp-0x30 (bucket 9)
    int16 dy, speedCalc;                    // var_34=dy at bp-0x34, var_32=speedCalc at bp-0x32 (bucket 10)
    int16 k;                                // dummy:  bp-0x36 (bucket 11)
    int16 idx;                              // var_38: bp-0x38 (bucket 12)
    int16 m;                                // dummy:  bp-0x3a (bucket 13)
    int16 knotsScale;                       // var_3C: bp-0x3c (bucket 14)
    int16 nsSign;                           // var_3E: bp-0x3e (bucket 15)

    if (g_initPhase == 0) {
        // (MSC stores chained assignments right-to-left:
        // ref store order is thrust, setThrust, velocity, 380D0, viewZ, roll, pitch)
        g_ourPitch =
            g_ourRoll =
                g_viewZ =
                    g_altitude =
                        g_velocity =
                            g_setThrust =
                                g_thrust = 0;

        if (gameData->difficulty == 0) {

            g_ourHead = ((g_viewY_ - (waypoints[1].mapY)) < 0x8000) ? 0 : 0x8000;
        } else {
            g_ourHead = (gameData->theater == 6)
                            ? 0                                       // If true, the result is 0
                            : ((gameData->theater & 1) ? 0 : 0x8000); // Else, evaluate the second condition
        }

        if (g_planeTable.planes[g_targetSlots[0].viewIndex].flags & 0x200) {
            *((uint8 *)&g_ourHead + 1) += 4;
        }

        rebuildOrientation();
        UpdateThrottleState();
        g_initPhase = 1;
    }

    keyScancode = 0;
    if (kbhit()) {
        keyScancode = egReadKey();
        if (g_autopilotEngaged == 1) {
            g_directorMode =
                g_autopilotEngaged =
                    keyValue = 0;
        }
    }

    while (kbhit()) {
        egReadKey(); // Flush keyboard buffer
    }

    // Main key dispatch logic
    switch (keyScancode) {
    case SCAN_MINUS:
        g_setThrust = clampRange(g_setThrust - 10, 0, 100);
        UpdateThrottleState();
        goto switch_break;
    case SCAN_EQUAL:
        g_setThrust = clampRange(g_setThrust + ((g_setThrust < 10) ? 5 : 10), 0, 100);
        UpdateThrottleState();
        *((uint8 *)&g_playerPlaneFlags) &= 0xF7; // ~8
        goto switch_break;
    case SCAN_A:
        g_setThrust = 0x90;
        UpdateThrottleState();
        *((uint8 *)&g_playerPlaneFlags) &= 0xF7; // ~8
        goto switch_break;
    case SCAN_SHIFT_EQUAL:
        g_setThrust = 100;
        UpdateThrottleState();
        *((uint8 *)&g_playerPlaneFlags) &= 0xF7; // ~8
        goto post_key_B_check;
    case SCAN_SHIFT_MINUS:
        g_setThrust = 0;
        makeSound(16, 0);
        UpdateThrottleState();
        goto switch_break;
    case SCAN_B:
        *((uint8 *)&g_playerPlaneFlags) ^= 8;
    post_key_B_check:
        if (!(*((uint8 *)&g_playerPlaneFlags) & 8) && g_groundAltitude != 0 && g_setThrust == 100) {
            g_velocity = 1350;
            makeSound(28, 2);
        }
        goto switch_break;
    case SCAN_ALT_J:
        if (g_joyCalibTimer == 0) {
            initJoystickCalibration();
            g_joyCalibTimer = 40;
        }
        goto switch_break;
    case SCAN_ALT_Q:
        finalizeMission(1);
        exitCode = 0;
        goto switch_break;
    case SCAN_ALT_B:
        if (g_hudVisible != 0) {
            gfx_captureToImage(g_eg2dBacking, *g_pageFront, 0, 97, 0, 97, 320, 103);
        }
        setDrawColor(COLOR_BLACK);
        fillRectBoth(0, 0, 319, 199);
        blitSprite(0, 0, 113, 55, 12, 7, 0);
        waitForKeyPress();
        if (g_hudVisible != 0) {
            gfx_restoreFromImage(g_eg2dBacking, *g_pageFront, 0, 97, 0, 97, 320, 103);
            UpdateThrottleState();
        }
        goto switch_break;
    case 0x1900: // Alt-P
        waitForKeyPress();
        goto switch_break;
    }

switch_break:
    if (g_joyCalibTimer != 0) {
        g_joyCalibTimer--;
    }

    if (g_setThrust != 0 && g_thrust == 0) {
        makeSound(14, 2);
    }

    if (g_inputDisabled != 0) {
        joyAxes[0] = 0;
        joyAxes[1] = 0;
    } else {
        if (input_preferGamepad()) {
            readCalibratedJoystick();
        } else {

            // temp_si = g_kbdSensitivity + 1;
            joyAxes[0] = (uint8)(((int16)((uint8)g_joyRawX - 0x80) * (g_kbdSensitivity + 1)) / 3) - 0x80;

            joyAxes[1] = (uint8)(((int16)((uint8)g_joyRawY - 0x80) * (g_kbdSensitivity + 1)) / 3) - 0x80;
        }
    }

    g_rollInput = ((uint16)joyAxes[0] >> 4) - 8;
    if (g_rollInput < 0) g_rollInput++;

    g_pitchInput = ((uint16)joyAxes[1] >> 4) - 8;
    if (g_pitchInput < 0) g_pitchInput++;

    g_rollInput = -((abs(g_rollInput) + 2) * g_rollInput) * 2;
    g_pitchInput *= 6;
    if (g_pitchInput < 0) {
        g_pitchInput /= 2;
    }

    if (g_groundAltitude == g_viewZ && g_pitchInput < 0 && g_ourPitch <= 0) {
        g_pitchInput = 0;
    }

    if (g_knots > 350 && !(*((uint8 *)&g_playerPlaneFlags) & 1) && g_gearDownArmed != 0) {
        g_gearDownArmed = 0;
        *((uint8 *)&g_playerPlaneFlags) |= 1;
        tempStrcpy("Landing gear raised");
        makeSound(32, 2);
    }

    if (g_groundAltitude == g_viewZ && g_setThrust == 0 && !(*((uint8 *)&g_playerPlaneFlags) & 8)) {
        *((uint8 *)&g_playerPlaneFlags) |= 8;
        tempStrcpy("Brakes on");
    }

    if (g_rollInput != 0 || g_pitchInput != 0) {
        g_autopilotAltitude = 0;
    }

    if (g_autopilotAltitude != 0) {
        headingErr = (g_autopilotEngaged != 0) ? ((g_missionTick & 0xF) << 8) - 0x800
                                               : 0;

        headingErr = egClampValue((int16)(headingErr - g_ourHead + g_waypointBearing), -5120, 0x1400) * 2;

        g_rollInput = -clampRange((int16)(headingErr - g_ourRoll) >> 6, -24, 24);

        tmpVal = egClampValue(((g_autopilotAltitude - g_viewZ) << 4) - g_rollPitchTrim, -5120, 0xC00);

        g_pitchInput = clampRange((tmpVal - g_ourPitch) >> 7, -8, 8);

        if (waypointIndex == 3) {
            nsSign = g_northSouthSign;
            tgtIdx = g_targetSlots[1].viewIndex;

            dx = g_planeTable.planes[tgtIdx].mapX - g_viewX_;
            dy = g_planeTable.planes[tgtIdx].mapY - g_viewY_;

            if (!(g_planeTable.planes[tgtIdx].flags & 0x200)) {
                nsSign = -signOf(dy);
            }

            dy += ((g_planeTable.planes[tgtIdx].flags & 0x200) ? 30 : 0x40) * nsSign;

            headingErr = abs((int16)g_ourHead);
            if (nsSign == -1) {
                dx = -dx;
                dy = -dy;
                headingErr = abs((int16)(g_ourHead - 0x8000));
            }

            tmpVal = clampRange((abs(dx) + abs(dy)) * 2 + headingErr / 32, 50, 0x1000);
            if (tmpVal < 0x1000) {
                exitSlowMotion();
            }

            if (g_planeTable.planes[tgtIdx].flags & 0x200) {
                tmpVal += 100;
            }

            if (g_inLandingCorridor != 0 && abs(headingErr) < 0x200) {
                tmpVal = -20;
            }

            dy = g_planeTable.planes[tgtIdx].mapY + (((g_planeTable.planes[tgtIdx].flags & 0x200) ? 28 : 56) * nsSign);

            dy += clampRange((abs(dx) * 4) + (headingErr / 16), 0, 0xC00) * nsSign;

            *((uint8 *)&g_playerPlaneFlags) &= 0xF7;

            if (headingErr > 0x4000) {
                dx = g_planeTable.planes[tgtIdx].mapX;
                tmpVal = 0x1000;
            } else {
                dx = g_planeTable.planes[tgtIdx].mapX + (nsSign * dx * 2);
                if (g_setThrust * 80 < g_knots) {
                    *((uint8 *)&g_playerPlaneFlags) |= 8;
                }
            }

            bearing = computeBearing(dx - g_viewX_, g_viewY_ - dy);
            knotsScale = g_knots / 16;

            headingErr = egClampValue((int16)(bearing - g_ourHead), (-knotsScale) << 8, knotsScale << 8) * 2;

            if (g_inLandingCorridor != 0) {
                headingErr = 0;
            }

            g_rollInput = -clampRange((int16)(headingErr - g_ourRoll) >> 6, -32, 32);

            g_setThrust = clampRange((abs(headingErr) / 256) + (tmpVal / 64), 35, 80);
            UpdateThrottleState();

            tmpVal = egClampValue(((tmpVal - g_viewZ) >> 3) + (g_rollPitchTrim >> 7), -24, 24);

            g_pitchInput = clampRange(tmpVal - (g_ourPitch >> 7), -16, 16);

            if (g_knots < 350) {
                *((uint8 *)&g_playerPlaneFlags) &= 0xFE;
            }

            if (g_groundAltitude == g_viewZ) {
                g_setThrust = 0;
                g_rollInput = 0;
                g_playerPlaneFlags |= 8;
                g_pitchInput = 0;
            }
        }
    }
    if (gameData->unk4 != 0) {
        turbulence = ((int32)g_knots * (1000 - g_viewZ)) >> 15;
    } else {
        turbulence = 0;
    }

    if (!((g_playerPlaneFlags) & 1)) {
        turbulence += clampRange((g_knots - 200) >> 5, 0, 32);
    }

    if (turbulence > 0 && ((uint16)g_groundAltitude) < ((uint16)g_viewZ)) {
        g_rollInput += randomRange(turbulence) - (turbulence >> 1);
        g_pitchInput += (randomRange(turbulence) - (turbulence >> 1)) >> 1;
    }

    if ((g_playerPlaneFlags & 1) && g_pitchInput <= 0 && ((uint16)g_stallSpeed) < ((uint16)g_velocity) && gameData->unk4 < 2 && abs((int16)g_ourRoll) < 0x3000 && g_gunFiredFlag == 0) {
        tmpVal = ((((g_rollPitchTrim) - (g_ourPitch)) >> 2) - g_viewZ + 300) >> 2;
        if (tmpVal > 0) {
            g_pitchInput = clampRange(tmpVal, 0, 32);
        }
    }

    if (g_ejectState != 0) {
        g_rollInput = 0x40;

        g_pitchInput = (abs((int16)g_ourRoll) > 0x4000) ? 0x10 : -8; // pitch_input_modifier

        // g_ejectState++;
        g_crashCamZ += clampRange(
            -(++g_ejectState - 0x20),
            (int16)0xFF00 / g_frameRateScaling,
            (int16)0x80 / g_frameRateScaling);
        // g_crashCamZ += stall_decay_effect;

        if (g_crashCamZ < 0) {
            g_crashCamZ = 0;
            if ((g_missionTick & 7) == 0) {
                finalizeMission(0);
            }
        }

        if (g_viewZ == 0 && g_smokeSourceIdx == -1) {
            g_smokeSourceIdx = 0;
            g_planeTable.planes[0].mapX = g_viewX_;
            g_planeTable.planes[0].mapY = g_viewY_;
            g_hitMapX = g_viewX_;
            g_hitMapY = g_viewY_;
            g_hitAlt = 0;
            g_hitEffectTimer = -8;
            makeSound(2, 2);
            g_velocity =
                g_setThrust = 0;
        }

        if ((g_ejectState & 0xFFFC) == 0x10 && (frameTick & 3) == 1) {
            g_smokeSourceIdx = -1;

            idx = ((uint16)frameTick / 2) & 7;

            g_particles[idx].posX = g_viewX_;
            g_particles[idx].posY = g_viewY_;
            g_particles[idx].alt = g_viewZ;

            g_particles[idx].spin = randomRange(0x20) << 11;

            g_smokeParticleSlot = idx;
            g_hitMapX = g_viewX_;
            g_hitMapY = g_viewY_;
            g_hitAlt = g_viewZ;
            g_hitEffectTimer = -8;
            makeSound(0, 2);

            g_ourPitch = -0x4000;
            g_orientationDirty = 1;
        }
    }

    if (g_gunHits != 0) {
        if (g_setThrust > -((g_gunHits * 4) - 0x90)) {
            g_setThrust = -((g_gunHits * 4) - 0x90);
            if (g_setThrust < 0)
                g_setThrust = 0;
            UpdateThrottleState();
        }
    }

    g_thrust += ((g_setThrust - g_thrust) / 4) / g_frameRateScaling;
    if (g_setThrust > g_thrust) g_thrust++;
    if (g_setThrust < g_thrust) g_thrust = g_setThrust;

    if ((((uint16)frameTick) % ((uint16)(g_frameRateScaling << 1))) == 0 && g_setThrust != 0 && g_autopilotEngaged == 0) {
        g_fuelRemaining -= ((g_setThrust * g_setThrust) / 750) + 2;
        drawFuelGauge();
    }

    if (g_fuelRemaining <= 0) {
        g_thrust = 0;
        g_fuelRemaining = 0;
    }

    g_gees = g_rollGeeTable[(abs((int16)g_ourRoll) >> 8) & 0x7f];
    if (((uint16)g_groundAltitude) < ((uint16)g_viewZ)) {
        g_gees += g_pitchInput / 2;
    }

    if (g_gees > 0x80) {
        g_gees = 0x80;
        g_pitchInput = clampRange(0x80 - g_rollGeeTable[(abs((int16)g_ourRoll) >> 8) & 0x7f], 0, g_pitchInput);
    }

    strcpy(g_geeStringBuf, itoa(g_gees / 16, strBuf, 10));
    strcat(g_geeStringBuf, ".");

    strcat(g_geeStringBuf, itoa((abs(g_gees) & 0xF) >> 1, strBuf, 10));
    strcat(g_geeStringBuf, "G");

    speedCalc = ((int32)(g_thrust - sinMul(g_ourPitch, 80)) * 800L) / 100L;

    g_cornerSpeed = 100;
    speedCalc = ((((uint16)g_viewZ >> 7) + 0x0400) * (int32)(speedCalc & speedCalc)) >> 10; // speedCalc & speedCalc folds to a plain load but ranks the operand "heavy", so the shift-expr is evaluated/pushed first like the ref

    g_cornerSpeed = ((int32)100 * (uint32)((g_altitude >> 6) + 0x0400)) >> 10;

    speedCalc = ((int32)speedCalc) * ((int32)(-((g_fuelRemaining >> 9) - 100))) / (int32)90;

    speedCalc = (((int32)speedCalc) * ((int32)(128 - g_gees))) >> 7;

    g_cornerSpeed = ((int32)isqrt(g_gees * 4) * (int32)g_cornerSpeed) >> 3;
    g_cornerSpeed = abs(g_cornerSpeed);

    if (!(*((uint8 *)&g_playerPlaneFlags) & 1)) {
        speedCalc -= speedCalc >> 3;
    }

    g_stallSpeed = g_cornerSpeed * 27;
    targetVel = clampRange(speedCalc, 0, 899) * 27;

    g_velocity += ((((int32)targetVel - g_velocity) / 16) / (int32)g_frameRateScaling);

    g_liftForce = ((int32)g_stallSpeed * 3072) / (abs(g_velocity) + 1);
    if ((uint16)g_liftForce > 0x2000) g_liftForce = 0x2000;

    g_rollPitchTrim = cosMul(g_ourRoll, g_liftForce - 0x300);

    if (*((uint8 *)&g_playerPlaneFlags) & 8) {
        if (g_groundAltitude == g_viewZ) {
            g_velocity -= (-((gameData->unk4 * 8) - 32) * 27) / g_frameRateScaling;
            if (g_groundAltitude != 0 && (uint16)g_velocity < 0x1B0) {
                g_velocity = 0;
            }
        } else {
            g_velocity -= ((uint16)g_velocity >> 4) / g_frameRateScaling;
        }
    }

    if ((uint16)g_velocity > 0xAFC8) g_velocity = 0;

    horizVel = cosMul(g_ourPitch, g_velocity);
    g_knots = (uint16)g_velocity / 27;

    audio_setEnginePitch(g_knots, g_thrust);

    yaw = (((int32)sinMul(g_ourRoll, g_gees << 4)) << 7) / ((int32)((int16)((uint16)g_velocity >> 9) + 0x20));

    yaw = cosMul(g_ourPitch, yaw);

    if (g_groundAltitude == g_viewZ) {
        yaw = (g_rollInput * -1) << 6;
        g_rollInput = 0;
        if (g_knots < g_cornerSpeed) {
            g_pitchInput = 0;
        }
    }

    if (g_autoCrashDive != 0) {
        g_pitchInput = -0x400 - g_ourPitch;
        // (ref stores velocity then setThrust; chains store right-to-left)
        g_setThrust =
            g_velocity = 0;
    }

    rollAngle = (((int32)g_rollInput) << 7) / ((int32)g_frameRateScaling);
    if (rollAngle != 0) {
        g_rollMatrix[4] = g_rollMatrix[0] = cosine(rollAngle);
        g_rollMatrix[1] = sine(rollAngle);
        g_rollMatrix[3] = -g_rollMatrix[1];
        applyRotationDelta(g_orientMatrix, g_rollMatrix);
    }

    pitchAngle = (int16)((int32)g_pitchInput << 7) / g_frameRateScaling;
    if (pitchAngle != 0) {
        g_pitchMatrix[8] = g_pitchMatrix[4] = cosine(pitchAngle);
        g_pitchMatrix[7] = sine(pitchAngle);
        g_pitchMatrix[5] = -g_pitchMatrix[7];
        applyRotationDelta(g_orientMatrix, g_pitchMatrix);
    }

    yawAngle = yaw / g_frameRateScaling;
    if (yawAngle != 0) {
        g_yawMatrix[8] = g_yawMatrix[0] = cosine(yawAngle);
        g_yawMatrix[2] = sine(yawAngle);
        g_yawMatrix[6] = -g_yawMatrix[2];
        applyRotationDelta(g_yawMatrix, g_orientMatrix);
    }

    computeAttitudeAngles();

    if ((uint16)g_stallSpeed > (uint16)g_velocity && (uint16)g_groundAltitude < (uint16)g_viewZ) {
        g_ourPitch -= ((uint16)g_stallSpeed - (uint16)g_velocity) >> ((gameData->unk4 == 2 || g_gunHits > 8) ? 1 : 2);
        g_orientationDirty = 1;
        if (g_ourPitch < 0 || (uint16)g_viewZ < 200) {
            makeSound(20, 1);
        }
    }

    if (g_groundAltitude == g_viewZ) {
        if (g_ourRoll != 0) {
            g_ourRoll = 0;
            g_orientationDirty = 1;
        }
        if (g_ourPitch < 0 || (g_ourPitch > 0 && g_knots < g_cornerSpeed)) {
            if (g_autoCrashDive == 0) {
                g_ourPitch = 0;
            }
            g_orientationDirty = 1;
        }
    }

    g_autoCrashDive = 0;

    g_highGeeFlag[0] = ((abs(g_ourPitch)) - (abs((int16)g_ourRoll) / 2) > 0x1000) ? 1 : 0;

    if (g_orientationDirty) {
        rebuildOrientation();
    }

    prevAlt = g_altitude;
    g_climbRate = fixedMulQ14((((uint16)g_velocity) / 10), sine(g_ourPitch - g_rollPitchTrim));

    if (g_autoLandingActive == 0) {
        g_altitude += (g_climbRate / g_frameRateScaling);

        g_ViewX += fixedMulQ14(horizVel, sine(g_ourHead)) / 10 / g_frameRateScaling;

        g_ViewY += fixedMulQ14(horizVel, cosine(g_ourHead)) / 10 / g_frameRateScaling;
    }

    if ((uint16)g_altitude > 0xf230 || (uint16)g_altitude < (uint16)g_groundAltitude) {
        g_altitude = g_groundAltitude;
    }
    if (g_altitude > 0xEA60) g_altitude = 0xEA60;

    if (g_altitude < 0x2000) {
        g_viewZ = g_altitude;
    } else if (g_altitude < 0x4000) {
        g_viewZ = ((g_altitude - 0x2000) >> 1) + 0x2000;
    } else {
        g_viewZ = ((g_altitude - 0x4000) >> 2) + 0x3000;
    }

    if (g_groundAltitude == g_viewZ) {
        if (prevAlt > g_groundAltitude && g_inLandingCorridor != 0) {
            makeSound(12, 2);
            // temp_bx = g_closestThreatIndex << 4;

            if (((((g_planeTable.planes[g_closestThreatIndex].flags & 0x200) ? 0x100 : 0x80) < ((int16)(-g_climbRate * g_missionStatus) / 2))) ||
                ((gameData->unk4 != 0 &&
                  (((g_playerPlaneFlags & 1) != 0) ||
                   (((int16)abs(g_ourRoll)) > (int16)((0x30 / (g_missionStatus + 1)) << 8)))))) {
                makeSound(0, 2);
                waitFrameSync(60);
                finalizeMission(5);
            }
        }
        g_climbRate = 0;
    }

    idx = frameTick & 0xF;
    g_viewSnapshotRing[idx].heading = g_ourHead;
    g_viewSnapshotRing[idx].pitch = g_ourPitch;
    g_viewSnapshotRing[idx].roll = g_ourRoll;
    *(int32 *)&g_viewSnapshotRing[idx].worldX = g_ViewX;
    *(int32 *)&g_viewSnapshotRing[idx].worldY = g_ViewY;
    g_viewSnapshotRing[idx].alt = g_viewZ;

    if (g_currentWeaponType == 1) {
        if (g_airTargetLock >= 0) {
            idx = clampRange((rangeApprox(g_viewX_ - g_simObjects[g_airTargetLock].posX, g_viewY_ - g_simObjects[g_airTargetLock].posY) * g_frameRateScaling) >> 8, 0, 12);

        } else {
            idx = g_frameRateScaling - 1;
        }

        idx = (frameTick - idx) & 0xF;

        headingErr = g_ourHead - g_viewSnapshotRing[idx].heading;
        tmpVal = g_ourPitch - g_viewSnapshotRing[idx].pitch;

        g_aamSeekerX = cosMul(g_ourRoll, ((-headingErr) >> 2)) + sinMul(g_ourRoll, (tmpVal >> 2));

        g_aamSeekerY = sinMul(g_ourRoll, (headingErr >> 2)) + cosMul(g_ourRoll, (tmpVal >> 1));
    }
}

void applyRotationDelta(const int16 *matA, const int16 *matB) {
    int16 p, a;

    g_rotationCounter++;
    if (!(*(char *)&g_rotationCounter & 7)) {
        g_orientationDirty = 1;
    }
    multiplyMatrix3x3Far(matA, matB, g_matrixScratch);
    memcpy(g_orientMatrix, g_matrixScratch, 18);
}

void computeAttitudeAngles(void) {
    int16 cosPitch;

    g_ourPitch = valueToAngle(-g_orientMatrix[5]);
    cosPitch = cosine(g_ourPitch);
    if (cosPitch != 0) {
        /* signedRatio16 returns the DOS 16-bit word pattern; the sign lives in
         * bit 15, so recover it as int16 before abs (a plain (int) leaves a
         * negative ratio as a huge positive and garbles the decoded angle). */
        if (abs(g_orientMatrix[2]) < 0x5a81) {
            g_ourHead = valueToAngle(abs((int16)signedRatio16(g_orientMatrix[2], cosPitch)));
        } else {
            g_ourHead = complementAngle(abs((int16)signedRatio16(g_orientMatrix[8], cosPitch)));
        }
        if (g_orientMatrix[2] <= 0 && g_orientMatrix[8] < 0) {
            (*((char *)&g_ourHead + 1)) += 0x80;
        }
        if (g_orientMatrix[2] > 0 && g_orientMatrix[8] < 0) {
            g_ourHead = 0x8000 - g_ourHead;
        }
        if (g_orientMatrix[2] < 0 && g_orientMatrix[8] > 0) {
            g_ourHead = -g_ourHead;
        }
        if (abs(g_orientMatrix[3]) < 0x5a81) {
            g_ourRoll = valueToAngle(abs((int16)signedRatio16(g_orientMatrix[3], cosPitch)));
        } else {
            g_ourRoll = complementAngle(abs((int16)signedRatio16(g_orientMatrix[4], cosPitch)));
        }
        if (g_orientMatrix[3] <= 0 && g_orientMatrix[4] < 0) {
            *((char *)&g_ourRoll + 1) += 0x80;
        }
        if (g_orientMatrix[3] > 0 && g_orientMatrix[4] < 0) {
            g_ourRoll = 0x8000 - g_ourRoll;
        }
        if (g_orientMatrix[3] < 0 && g_orientMatrix[4] > 0) {
            /* Force MSC to emit sub ax, ax; sub ax, g_ourRoll. */
            g_ourRoll = 0x10000 - g_ourRoll;
        }
    } else {
        g_ourRoll = 0;
        g_ourHead = valueToAngle(g_orientMatrix[1]);
        if (g_orientMatrix[3] <= 0 && g_orientMatrix[4] < 0) {
            (*((char *)&g_ourHead + 1)) += 0x80;
        }
        if (g_orientMatrix[3] > 0 && g_orientMatrix[4] < 0) {
            g_ourHead = 0x8000 - g_ourHead;
        }
        if (g_orientMatrix[3] < 0 && g_orientMatrix[4] > 0) {
            g_ourHead = -g_ourHead;
        }
    }
    if (g_ourPitch > 0x38e3 && g_ourPitch < 0x4001) {
        g_orientationDirty = 1;
    }
    if (g_ourPitch < (int16)0xc71d && g_ourPitch > (int16)0xbfff) {
        g_orientationDirty = 1;
    }
    if (g_rollWasNonzero != 0 && g_ourRoll == 0) {
        g_orientationDirty = 1;
    }
}

void rebuildOrientation() {
    buildRotationMatrixFar(g_orientMatrix, g_ourHead, g_ourPitch, g_ourRoll);
    g_orientationDirty = 0;
    g_rotationCounter = 0;
}

uint16 signedRatio16(int16 numerator, int16 denominator) { /* Original: IntDiv(A,B). Divide two signed 15-bit fractions. */
    char numeratorSign = 1;
    char denominatorSign = 1;
    int32 absNumerator;
    int32 absDenominator;

    /* Divide two signed 15-bit fractions, then restore the combined sign. */
    if (numerator < 0) numeratorSign = -1;
    if (denominator < 0) denominatorSign = -1;
    absNumerator = (int32)(numerator < 0 ? -numerator : numerator);
    absDenominator = (int32)(denominator < 0 ? -denominator : denominator);
    /* Original callers consume the 16-bit quotient word, including wraparound
     * after applying the sign. Keep the DOS word pattern instead of returning a
     * host-width signed arithmetic result. */
    return (uint16)((uint16)((((uint32)(uint16)absNumerator) << 16) / (uint32)absDenominator >> 1) *
                    (int16)numeratorSign * (int16)denominatorSign);
done:;
}

#define ASIN_TABLE_SHIFT 9
#define WORD_DEGREE_STEP 256

int16 valueToAngle(int16 value) { /* Original: Iasin(A). Return 16-bit word-degree arcsin by table interpolation. */
    int16 angle, magnitude, tableIndex, tableSpan;

    if (value == (int16)0x8000) return (int16)0xc000;
    magnitude = abs(value);
    tableIndex = (magnitude >> ASIN_TABLE_SHIFT) + 1;
    for (; tableIndex >= 0; tableIndex--) {
        if (g_angleLut[tableIndex] <= magnitude) {
            tableSpan = g_angleLut[tableIndex + 1] - g_angleLut[tableIndex];
            angle = (int16)((int32)(magnitude - g_angleLut[tableIndex]) * WORD_DEGREE_STEP / (int32)tableSpan) + tableIndex * WORD_DEGREE_STEP;
            break;
        }
    }
    if (value < 0) {
        angle = -angle;
    }
    return angle;
}

int16 complementAngle(int16 value) { /* Original: Iacos(A). Return 16-bit word-degree arccos as quarter-turn minus arcsin. */
    enum { WORD_DEGREES_QUARTER_TURN = 0x4000 };
    return WORD_DEGREES_QUARTER_TURN - valueToAngle(value);
}

int16 isqrt(int16 value) { /* Original: Sqrt(N). Return integer square root using Newton iteration. */
    int16 quotient, guess;
    /* Integer square root using Newton iteration seeded from value >> 2. */
    value = abs16Compat(value);
    if (value < 4) {
        return 1;
    }
    guess = value >> 2;
    do {
        quotient = value / guess;
        guess = (guess + quotient) >> 1;
    } while (abs16Compat(guess - quotient) > 1);
    return guess;
}

/* Q12 shortest-arc tween that snaps across the 0x8000 gimbal flip (see lerpAngle
 * in egsys.c) rather than sweeping the view 180deg for one frame. */
static int lerpViewAngle(int a0, int a1, int alpha) {
    int16 d = (int16)(a1 - a0);
    if (d >= 0x4000 || d <= -0x4000)
        return a1;
    return a0 + ((d * alpha) >> 12);
}

/* A gimbal flip rewrites the whole head/pitch/roll triple (see lerpPose in
 * egsys.c): if any component snaps, take the full new pose instead of mixing a
 * snapped component with still-tweening ones. */
static int poseSnapsQ12(const struct ViewSnapshot *s0, const struct ViewSnapshot *s1) {
    int16 dh = (int16)(s1->heading - s0->heading);
    int16 dp = (int16)(s1->pitch - s0->pitch);
    int16 dr = (int16)(s1->roll - s0->roll);
    return dh >= 0x4000 || dh <= -0x4000 || dp >= 0x4000 || dp <= -0x4000 ||
           dr >= 0x4000 || dr <= -0x4000;
}

/* split a Q8 fixed-point eye coordinate into the integer part + [0,255] frac */
static int32 eyeFromQ8(long q8, int16 *frac) {
    *frac = (int16)(q8 & 0xff);
    return (int32)(q8 >> 8);
}

// something to do with view switching?
void renderFrame() {
    int16 camDist, savedCamDist, range, camOffset, dx, dy, tmp;
    g_camEyeX = g_viewTargetX = g_ViewX;
    g_camEyeY = g_ViewY;
    g_viewTargetY = 0x100000 - g_ViewY;
    g_camEyeZ = g_viewZ + 0x18;
    g_camEyeFracX = g_camEyeFracY = g_camEyeFracZ = 0;
    g_viewTargetAlt = g_viewZ;
    camDist = g_externalCamDist = clampRange(g_externalCamDist, 2, 8);
    switch (keyValue) {
    case 0:
    case 0x44:
        g_viewHeading = g_ourHead;
        g_viewPitch = g_ourPitch;
        g_viewRoll = g_ourRoll;
        break;
    case 0x41:
        g_viewHeading = g_ourHead + 0x8000;
        g_viewPitch = -g_ourPitch;
        g_viewRoll = -g_ourRoll;
        break;
    case 0x43:
        g_viewHeading = g_ourHead + 0x4000;
        g_viewPitch = -g_ourRoll;
        g_viewRoll = g_ourPitch;
        break;
    case 0x42:
        g_viewHeading = g_ourHead - 0x4000;
        g_viewPitch = g_ourRoll;
        g_viewRoll = -g_ourPitch;
        break;
    case 0x84: {
        /* The trailing replay camera reads a pose from the per-sim-step history
         * ring; interpolate between the two samples bracketing the delayed view
         * time (render alpha, Q12) so it tracks smoothly instead of stepping at
         * the sim rate. */
        int r1, a = g_renderAlphaQ12;
        struct ViewSnapshot *s0, *s1;
        tmp = (frameTick - ((g_frameRateScaling + 1) / 2) - 1) & 0xf;
        r1 = (tmp + 1) & 0xf;
        s0 = &g_viewSnapshotRing[tmp];
        s1 = &g_viewSnapshotRing[r1];
        if (poseSnapsQ12(s0, s1)) {
            g_viewHeading = s1->heading;
            g_viewPitch = s1->pitch;
            g_viewRoll = s1->roll;
        } else {
            g_viewHeading = lerpViewAngle(s0->heading, s1->heading, a);
            g_viewPitch = lerpViewAngle(s0->pitch, s1->pitch, a);
            g_viewRoll = lerpViewAngle(s0->roll, s1->roll, a);
        }
        g_camEyeX = s0->worldX + (int32)(((int64)(s1->worldX - s0->worldX) * a) >> 12);
        g_camEyeY = s0->worldY + (int32)(((int64)(s1->worldY - s0->worldY) * a) >> 12);
        g_camEyeZ = s0->alt + (((int32)(s1->alt - s0->alt) * a) >> 12);
        break;
    }
    case 0x85:
        g_viewHeading = g_ourHead - 0x4000;
        g_viewPitch = 0;
        g_viewRoll = 0;
        /* Q8 eye: a whole-fine-unit eye position lurches visibly at close cam
         * distance as the offset rotates with the (smoothly interpolated) heading. */
        g_camEyeX = eyeFromQ8(sinMulQ8(g_ourHead + 0x4000, 0x18 << camDist) + ((long)g_ViewX << 8), &g_camEyeFracX);
        g_camEyeY = eyeFromQ8(cosMulQ8(g_ourHead + 0x4000, 0x18 << camDist) + ((long)g_ViewY << 8), &g_camEyeFracY);
        break;
    case 0x86:
        g_viewHeading = 0x8000;
        g_viewPitch = 0;
        g_viewRoll = 0;
        g_camEyeY = (0x18 << camDist) + g_ViewY;
        break;
    case 0x87:
        g_viewHeading = g_ourHead;
        g_viewPitch = 0;
        g_viewRoll = 0;
        g_camEyeX = eyeFromQ8(sinMulQ8(g_ourHead + 0x8000, 0x18 << camDist) + ((long)g_ViewX << 8), &g_camEyeFracX);
        g_camEyeY = eyeFromQ8(cosMulQ8(g_ourHead + 0x8000, 0x18 << camDist) + ((long)g_ViewY << 8), &g_camEyeFracY);
        g_camEyeZ = (4 << camDist) + g_viewZ;
        break;
    case 0x88:
    case 0x89:
    case 0x8b:
        if (keyValue != 0x89) {
            if (g_currentWeaponType == 1) {
                // XXX: test byte ptr g_airTargetLock, 80h -> check which byte is tested, other byte ptr instructions in this routine
                if (!(g_airTargetLock & 0x80)) g_viewTargetObj = g_airTargetLock + 0x20;
            } else {
                if (!(g_groundTargetLock & 0x80)) g_viewTargetObj = g_groundTargetLock + 0x40;
            }
        } else {
            if (g_directorMode == 0) g_viewTargetObj = g_lastMissileSlot;
        }
        savedCamDist = camDist;
        if (!(g_viewTargetObj & 0x40)) {
            if (!(g_viewTargetObj & 0x20)) {
                if (g_projectiles[g_viewTargetObj].ttl != 0) {
                    /* Fine (sub-mapX-unit) interpolated position so the tracking
                     * camera doesn't lurch in 32-unit steps (the "earthquake"). */
                    g_viewTargetX = (uint32)g_projInterpX[g_viewTargetObj];
                    g_viewTargetY = (uint32)g_projInterpY[g_viewTargetObj];
                    g_viewTargetAlt = g_projectiles[g_viewTargetObj].alt;
                } else {
                    g_projectiles[g_viewTargetObj].worldX = g_ourHead;
                    g_projectiles[g_viewTargetObj].worldY = g_ourPitch;
                    if (g_directorMode != 0) keyValue = 0x87;
                }
                camDist = 5;
            } else {
                // .... g_viewTargetObj & 0x1f
                g_viewTargetX = g_simObjects[g_viewTargetObj & 0x1f].worldX;
                g_viewTargetY = g_simObjects[g_viewTargetObj & 0x1f].worldY;
                g_viewTargetAlt = g_simObjects[g_viewTargetObj & 0x1f].alt;
                camDist = 5;
            }
        } else {
            g_viewTargetX = (uint32)g_planeTable.planes[g_viewTargetObj & 0x3f].mapX << 5;
            g_viewTargetY = (uint32)g_planeTable.planes[g_viewTargetObj & 0x3f].mapY << 5;
            g_viewTargetAlt = g_planeTable.planes[g_viewTargetObj & 0x3f].flags & 0x200 ? 200 : 50;
            camDist = 7;
            if (g_autopilotEngaged != 0 && g_directorEventDeadline == -1) camDist = 6;
        }
        if (g_directorMode == 0) camDist = savedCamDist;
        /* Derive the tracking-camera heading and pitch from FINE world coords —
         * both the fine target position and the fine player position (g_ViewX/Y),
         * not the coarse map coords (g_viewX_/g_viewY_) which step 32 fine units
         * at a time and made the whole world "earthquake" around a tracked target.
         * computeBearing is scale-invariant, so the angles are identical to the
         * original coarse formula but with ~32x less quantization jitter. range is
         * computed inline (rangeApprox would saturate its 0x7fff cap on fine
         * deltas) and kept in the same scale as the altitude delta so the pitch
         * ratio is unchanged. */
        dx = (int)(g_viewTargetX - g_ViewX);
        dy = (int)(g_viewTargetY - g_ViewY);
        {
            int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
            range = adx > ady ? adx + (ady >> 1) : ady + (adx >> 1);
        }
        g_viewHeading = computeBearing(dx, -dy);
        g_viewPitch = -computeBearing(g_viewTargetAlt - g_viewZ, range);
        g_viewRoll = 0;
        camOffset = cosMul(g_viewPitch, 0x18 << camDist);
        if (g_viewTargetObj & 0x60 || g_directorMode != 0) {
            if (keyValue == 0x88) {
                g_camEyeX = eyeFromQ8(sinMulQ8(g_viewHeading + 0x8000, camOffset) + ((long)g_ViewX << 8), &g_camEyeFracX);
                g_camEyeY = eyeFromQ8(cosMulQ8(g_viewHeading + 0x8000, camOffset) + ((long)g_ViewY << 8), &g_camEyeFracY);
                g_camEyeZ = (int16)eyeFromQ8(sinMulQ8(g_viewPitch, 0x18 << camDist) + ((long)((4 << camDist) + g_viewZ) << 8), &g_camEyeFracZ);
                g_viewPitch = -g_viewPitch;
            } else {
                g_camEyeX = eyeFromQ8(sinMulQ8(g_viewHeading, camOffset) + ((long)g_viewTargetX << 8), &g_camEyeFracX);
                g_camEyeY = eyeFromQ8(cosMulQ8(g_viewHeading, camOffset) + (((long)0x100000 - (long)g_viewTargetY) << 8), &g_camEyeFracY);
                g_camEyeZ = (int16)eyeFromQ8((((long)(4 << camDist) + g_viewTargetAlt) << 8) - sinMulQ8(g_viewPitch, 0x18 << camDist), &g_camEyeFracZ);
                if (g_viewTargetObj & 0x40 && g_planeTable.planes[g_viewTargetObj & 0x3f].flags & 0x200 && g_camEyeZ < 132) {
                    g_camEyeZ = 132;
                    g_camEyeFracZ = 0;
                }
                g_viewHeading += 0x8000;
            }
        } else {
            g_viewHeading = g_projectiles[g_viewTargetObj].worldX;
            g_viewPitch = g_projectiles[g_viewTargetObj].worldY - 0x400;
            camOffset = cosMul(g_viewPitch, 0x10 << camDist);
            g_camEyeX = eyeFromQ8(((long)g_viewTargetX << 8) - sinMulQ8(g_viewHeading, camOffset), &g_camEyeFracX);
            g_camEyeY = eyeFromQ8(((long)0x100000 << 8) - (cosMulQ8(g_viewHeading, camOffset) + ((long)g_viewTargetY << 8)), &g_camEyeFracY);
            g_camEyeZ = (int16)eyeFromQ8(((long)g_viewTargetAlt << 8) - sinMulQ8(g_viewPitch, 0x10 << camDist), &g_camEyeFracZ);
        }
        break;
    case 0x8c:
        g_viewPitch = 0xf400;
        g_viewRoll = 0;
        g_camEyeX = (int32)g_crashCamX << 5;
        g_camEyeY = (0x8000 - (int32)g_crashCamY) << 5;
        g_camEyeZ = g_crashCamZ;
        break;
    }
    if (abs(g_viewPitch) > 0x4000 || g_viewPitch == 0x8000) {
        g_viewPitch = 0x8000 - g_viewPitch;
        g_viewHeading += 0x8000;
        g_viewRoll = 0x8000 - g_viewRoll;
    }
    if (keyValue == 0) {
        memcpy(g_camRotMatrix, g_orientMatrix, 18);
    } else {
        buildRotationMatrixFar(g_camRotMatrix, g_viewHeading, g_viewPitch, g_viewRoll);
    }
    if (g_camEyeZ < 0x10) {
        g_camEyeZ = 0x10;
        g_camEyeFracZ = 0;
    }
    tmp = g_hudVisible;
    g_hudVisible = (keyValue & 0xc0) == 0;
    if (tmp != g_hudVisible) {
        gfx_waitRetrace();
        if (g_hudVisible != 0) {
            gfx_nop23();
            gfx_restoreFromImage(g_eg2dBacking, *g_pageFront, 0, 97, 0, 97, 320, 103);
            UpdateThrottleState();
            drawWeaponAmmo();
            drawWeaponSelectMarker(missileSpecIndex);
            if (g_mapMode == 0) {
                redrawTacMap(g_viewX_, g_viewY_);
            }
            g_groundTargetLock = g_airTargetLock = 0xffff;
            fillPanelBox(3, 3);
            g_lockedTargetKilled = 0;
        } else {
            gfx_captureToImage(g_eg2dBacking, *g_pageFront, 0, 97, 0, 97, 320, 103);
        }
    }
    if (keyValue != g_lastViewKey) {
        if (keyValue == 0x42 || keyValue == 0x43 || keyValue == 0x41) {
            gfx_waitRetrace();
            if (gfx_getModecode() == 3) {
                openBlitClosePic(keyValue == 0x42 ? "256Left.Pic" : keyValue == 0x43 ? "256Right.Pic"
                                                                                     : "256Rear.Pic",
                                 *g_pageFront);
            } else {
                openBlitClosePic(keyValue == 0x42 ? "Left.Pic" : keyValue == 0x43 ? "Right.Pic"
                                                                                  : "Rear.Pic",
                                 *g_pageFront);
            }
            g_pageFront[8] = 96;
        } else {
            g_pageFront[8] = g_hudVisible != 0 ? 96 : 199;
        }
        g_lastViewKey = keyValue;
    }
    g_horizonGroundColor = g_world3dData[47];
    *(uint8 *)(&g_skyColorIndex) = 3;
    if (g_detailLevel == 0) {
        g_horizonGroundColor = 3;
        *(uint8 *)(&g_skyColorIndex) = 0x0b;
    }
    loadColorPalette(g_nightMode);
    *(uint8 *)(&g_posVisibleFlag) = 0;
    render3DView(-g_viewHeading, g_viewPitch, g_viewRoll, g_camEyeX, g_camEyeY, (int32)g_camEyeZ, 0, 0, 320, g_pageFront[8] + 1);
    g_extraScaleShift = 0;
    g_savedPosVisible = g_posVisibleFlag;
    if (keyValue == 0x41) {
        drawVectorShape(g_rearViewShape);
        gfx_setColor(0xf);
        g_lineX1 = 241;
        g_lineY1 = 21;
        g_lineX2 = 251;
        g_lineY2 = 94;
        drawClipLineGlobal();
        g_lineX1 = 83;
        g_lineY1 = 21;
        g_lineX2 = 73;
        g_lineY2 = 94;
        drawClipLineGlobal();
        gfx_nop23();
        blitSprite(107, 48, 209, 0, 111, 47, 0);
        blitSprite(65, 95, 125, 54, 195, 2, 0);
    }
    g_hudBottomY = (g_activePanelMode == 0x13 || g_mapMode == 1 || g_hudVisible == 0) ? 200 : 97;

    /* Non-retained renderers (GL) rebuild the 2D overlay every present, so the
     * tac map — cached into g_eg2dBacking for the software page — must be re-emitted
     * into this frame's native stream, crisp at window resolution. Runs inside the
     * vector frame (render3DView opened it above). It reloads colorLut to the map
     * palette; save/restore it so the HUD/gauges drawn after keep the flight
     * palette render3DView set. The software path leaves the cached map alone. */
    if (!r2d_overlayRetained()) {
        uint8 savedLut[16];
        memcpy(savedLut, colorLut, 16);
        renderTacMapOverlay();
        memcpy(colorLut, savedLut, 16);
    }
}

void UpdateThrottleState(void) {
    if (g_hudVisible != 0) {
        setDrawColor(COLOR_BLACK);
        fillRectBoth(212, 127, 222, 175);
        setDrawColor(COLOR_LIGHTRED);
        fillRectBoth(212, -(g_setThrust / 3 - 175), 222, 175);
        if (100 < g_setThrust) {
            setDrawColor(COLOR_YELLOW);
            fillRectBoth(212, -(g_setThrust / 3 - 175), 222, 142);
        }
    }
}
void drawFuelGauge(void) {
    if (g_hudVisible == 0) {
        return;
    }
    setDrawColor(COLOR_BLACK);
    fillRectBoth(5, 109, 10, 152);
    setDrawColor(g_fuelRemaining > 2000 ? COLOR_GREEN : COLOR_YELLOW);
    fillRectBoth(5, -(g_fuelRemaining / 250 - 152), 10, 152);
}

void drawVectorShape(const int16 *shapeData) {
    while (*shapeData != -1) {
        gfx_setColor(colorLut[*shapeData++]);
        resetScanlineSpans();
        shapeData += 2;
        while (*shapeData != -1) {
            g_lineX1 = shapeData[-2];
            g_lineY1 = shapeData[-1];
            g_lineX2 = *shapeData++;
            g_lineY2 = *shapeData++;
            clipAndRasterizeEdge();
        }
        flushSpanDirtyRect();
        shapeData++;
    }
}

void waitForKeyPress(void) {
    int16 savedTiming;

    audio_engineDroneOff();
    savedTiming = g_frameTimingAccum;
loop:
    while (kbhit() == 0);
    if (egReadKey() == 0x1900)
        goto loop;
    updateEngineSound();
    g_frameTimingAccum = savedTiming;
}
