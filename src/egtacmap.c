// seg000 debug code (/Zi)
#include "eg3dmap.h"
#include "eg3dview.h"
#include "egcode.h"
#include "egcombat.h"
#include "egdata.h"
#include "egframe.h"
#include "egmath.h"
#include "egtacmap.h"
#include "egthreat.h"
#include "egtypes.h"
#include "egui.h"
#include "offsets.h"
#include "log.h"
#include "gfx.h"
#include "r2d.h"
#include "hdsprite.h"
#include "const.h"
#include "comm.h"

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private helpers for this translation unit. */
void egDrawStringCentered(int16 *, const char *, int, int, int);
void renderHudFrame();
int mapXToScreen();
int mapYToScreen();
void drawMapLine(int x1, int y1, int x2, int y2);
void drawColorPoint();
void drawMapPoint(int, int, int);
void drawPanelText(int, const char *, int);
int readScreenPixel(int screenX, int screenY);

// ==== seg000:0x8e38 ====

void clearStatusPanel(void) {
    drawPanelText(2, "", 0);
}

// ==== seg000:0x8e50 ====
void renderHudFrame(int unused) {
    int climbMarkerY, angleFixed, waypointMarkerX, circleX, angle, circleY, prevX, speedBarLen, prevY, markerX, deltaX, markerY, deltaY;
    char seekerShift;
    // probably x,y
    deltaX = waypoints[waypointIndex].mapX - g_viewX_;
    deltaY = waypoints[waypointIndex].mapY - g_viewY_;
    g_waypointBearing = computeBearing(deltaX, -deltaY);
    if (g_hudVisible != 0) {
        if (g_damageTakenFlag != 0) {
            g_damageTakenFlag = 0;
            if (!(g_viewMode & 0x80)) {
                setDrawColor(COLOR_FLAMING);
                /* Immediate on GL: the flash colour (COLOR_FLAMING = 0x0d) is a
                 * fire-cycled DAC entry, so baking it into the page would keep the
                 * cached backdrop texture perpetually dirty. Software bakes it and
                 * the DAC cycle animates it in place over the countdown. */
                fillSpanRectImmediate(g_pageFront, 0, 0, 319, 96);
                gfx_setDacAnimCount(60);
            }
        }
        g_hudDrawnFlag = 1;
        if (g_viewMode == VIEW_COCKPIT && g_halfScaleRender == 0) {
            // draw stick position indicator
            setDrawColor(COLOR_BLACK);
            drawViewportLine(277, 83, 293, 83);
            drawViewportLine(293, 83, 293, 95);
            drawViewportLine(293, 95, 277, 95);
            drawViewportLine(277, 95, 277, 83);
            drawViewportLine(285, 89, 285, 89);
            setDrawColor(COLOR_WHITE);
            markerX = ((int16)(joyAxes[0] - 120) >> 4) + 285;
            markerY = ((int16)((joyAxes[1] * 3) - 360) >> 6) + 89;
            drawViewportLine(markerX - 1, markerY, markerX + 1, markerY);
            drawViewportLine(markerX, markerY + 1, markerX, markerY - 1);
            if (g_playerPlaneFlags & 0x200) {
                setDrawColor(COLOR_WHITE);
                drawViewportLine(156, 89, 164, 89);
                drawViewportLine(160, 86, 160, 92);
            }
            setDrawColor(g_nightMode != 0 ? COLOR_RED : COLOR_BLACK);
            speedBarLen = clampRange((((g_cornerSpeed - g_knots) * 2) / 5) + 29, 0, 61);
            if (speedBarLen) drawViewportLine(72, 85 - speedBarLen, 72, 85);
            drawViewportLine(247, 56, 247, clampRange(-((g_climbRate >> 4) - 56), 20, 85));
            if ((g_playerPlaneFlags & 1) == 0 && (frameTick & 1) != 0 && gameData->unk4 != 0 && g_climbRate < 0) {
                climbMarkerY = (((g_planeTable.planes[g_closestThreatIndex].flags & 0x200 ? 0x100 : 0x80) / gameData->unk4) >> 4) + 56;
                setDrawColor(COLOR_WHITE);
                drawViewportLine(242, climbMarkerY - 2, 244, climbMarkerY);
                drawViewportLine(242, climbMarkerY + 2, 244, climbMarkerY);
            }
            // stall warning display
            if (g_knots < g_cornerSpeed && g_groundAltitude != g_viewZ && frameTick & 1) {
                drawStringActivePage("stall warning", 132, 30, 0xf);
            }
            if (g_currentWeaponType == 0 || g_currentWeaponType == 2) {
                setDrawColor(COLOR_LIGHTGRAY);
                g_flightPathMarkerY = (g_rollPitchTrim >> 6) + 56;
                if (g_flightPathMarkerY > 10 && g_flightPathMarkerY < 111) {
                    /* Fractional Y (the >>6 dropped) so the marker glides sub-pixel. */
                    float ry = g_rollPitchTrim / 64.0f + 56.0f;
                    if (!hdsprite_drawHudGunReticle(154.0f, ry - 4.0f)) {
                        blitSprite(154, g_flightPathMarkerY - 4, 0x94, 21, 11, 7, 0xf);
                    }
                }
            }
            if (g_currentWeaponType == 1) {
                seekerShift = g_halfScaleRender + 4;
                markerX = (g_aamSeekerX >> seekerShift) + 159;
                markerY = (g_aamSeekerY >> seekerShift) + 56;
                if (markerX > 10 && markerX < 309 && markerY > 8 && markerY < 91) {
                    /* Fractional position (the >>seekerShift dropped) for sub-pixel glide. */
                    float scale = 1.0f / (float)(1 << seekerShift);
                    float sx = g_aamSeekerX * scale + 159.0f;
                    float sy = g_aamSeekerY * scale + 56.0f;
                    if (!hdsprite_drawHudAamSeeker(sx - 6.0f, sy - 5.0f)) {
                        blitSprite(markerX - 6, markerY - 5, 0x91, 0x4, 0xd, 0xb, 0xe);
                    }
                }
                // 7 = air to air? Only Sidewinder and Amraam have it
                if (sams[missiles[missleSpec[missileSpecIndex].weaponIdx].specIndex].weaponClass == 7) {
                    setDrawColor(COLOR_LIGHTGRAY);
                    for (angle = 0; angle <= 0x100; angle += 0x10) {
                        angleFixed = angle << 8;
                        /* X radius 42 (not the original 40) so the A2A ring reads as
                         * a true circle on the non-square-pixel display: 84 wide x 70
                         * tall x the 1.2 present aspect = round. */
                        circleX = sinMul(angleFixed, 42) + 159;
                        circleY = -(cosMul(angleFixed, 35) - 56);
                        if (angle != 0) drawViewportLine(circleX, circleY, prevX, prevY);
                        prevX = circleX;
                        prevY = circleY;
                    }
                }
            }
            drawNumber(g_knots, 80, 54, 0xf);
            if (g_altitude <= 20000) {
                drawNumber(g_altitude < 100 ? g_altitude : (g_altitude / 5) * 5, 228, 54, 0xf);
            }
            if (g_slowMotionMode > 1) {
                drawStringBothPages("ACCEL", 150, 4, 0xf);
            }
            if (g_playerPlaneFlags & 0x1000) {
                drawStringBothPages("TRAINING", 234, 16, 0xf);
            }
            if (g_autopilotAltitude != 0) {
                drawStringBothPages("AUTOPILOT", 236, 90, 0xf);
            }
            waypointMarkerX = clampRange((((int16)(g_waypointBearing - g_ourHead) >> 6) / 3) + 159, 89, 229);
            setDrawColor(COLOR_LIGHTCYAN);
            drawViewportLine(waypointMarkerX - 2, 15, waypointMarkerX, 17);
            drawViewportLine(waypointMarkerX, 17, waypointMarkerX + 2, 15);
            drawViewportLine(waypointMarkerX - 2, 15, waypointMarkerX + 2, 15);
            goto somewhere;
        }
    somewhere:
        drawTacticalMap(0);
    }
    if (g_hudMsgTimer != 0 && ((g_viewMode == VIEW_COCKPIT && g_halfScaleRender == 0) || (g_directorMode != 0))) {
        drawStringActivePage(tempString, -(((int16)strlen(tempString) >> 1) - 40) * 4, 24, 0xf);
        g_hudMsgTimer--;
        if (g_hudMsgTimer == 0) { // cancel pending eject on message disappear
            g_ejectPending = 0;
        }
        if (g_autopilotEngaged == 1) {
            drawStringActivePage("Press any key to play", 120, 1, g_nightMode != 0 ? 0xe : 0);
        }
    }
    if (g_dirMsgTimer != 0 && g_viewMode == VIEW_COCKPIT && g_halfScaleRender == 0) {
        drawStringActivePage(string_3C04A, -(((int16)strlen(string_3C04A) >> 1) - 40) * 4, 90, 0xf);
        g_dirMsgTimer--;
    }
}

// ==== seg000:0x94d0 routine_189 ====
void setActivePanel(int panelId) {
    int p, a, b, c, d, e, f, g, h, i;
    if (g_hudVisible == 0) {
        return;
    }
    switch (panelId) {
    case 0x13:
        strcpy(strBuf, "TrackCam ");
        switch (g_viewHeadingOffset) {
        case 0:
            strcat(strBuf, "Ahead");
            break;
        case (int16)0x8000:
            strcat(strBuf, "Rear");
            break;
        case 0x4000:
            strcat(strBuf, "Right");
            break;
        case (int16)0xC000:
            strcat(strBuf, "Left");
            break;
        }
        drawPanelText(2, strBuf, 3);
        break;
    }
    g_activePanelMode = panelId;
}

// ==== seg000:0x957a ====
void refreshActivePanel(int panelId) {
    int p;
    if (panelId == g_activePanelMode) {
        setActivePanel(panelId);
    }
}

// ==== seg000:0x9595 ====
void initTacMapView(void) {
    g_mapMode = 0;
    g_scopeClipLeft = 24;
    g_scopeClipRight = 96;
    g_scopeClipTop = 112;
    g_scopeClipBottom = 168;
    g_scopeCenterX = 72;
    g_scopeCenterY = 56;
    zoomIn();
}

// ==== seg000:0x95c9 ====

/* Recompute the map centre and render the terrain + plane/waypoint markers into
 * the left-MFD region. Shared by redrawTacMap (which then caches the region to
 * g_eg2dBacking) and renderTacMapOverlay (the per-frame GL vector path). */
static void renderTacMapContent(int centerX, int centerY) {
    int16 screenX, screenY, idx;

    drawPanelText(1, "Map", 0);
    idx = 72 << (9 - g_mapZoomLevel);
    g_mapCenterX = clampRange(sinMul(g_ourHead, 0x4000 >> g_mapZoomLevel) + centerX, idx, 0x7fff - idx);
    idx = (56 << (9 - g_mapZoomLevel)) / 3 * 4;
    g_mapCenterY = clampRange(centerY - cosMul(g_ourHead, 0x4000 >> g_mapZoomLevel), idx, 0x7fff - idx);
    loadColorPalette(0);
    gfx_setFadeSteps(19);
    renderMapTerrain(g_mapTerrainMode, g_mapCenterX / 2, -(g_mapCenterY / 2 - 0x4000), 9 - g_mapZoomLevel);
    if (gameData->theater < 2) {
        gfx_setFadeSteps(12);
    } else {
        gfx_setFadeSteps(16);
    }
    for (idx = 1; idx < g_planeCount; idx++) {
        if (g_planeTable.planes[idx].active != 0 && !(g_planeTable.planes[idx].flags & 0x80) &&
            objectToScreen(g_planeTable.planes[idx].mapX, g_planeTable.planes[idx].mapY, &screenX, &screenY)) {
            blitSprite(screenX - 1, screenY - 1, 0xa4, 0, 4, 4, 0);
        }
        if (((g_planeTable.planes[idx].flags & 0x481) == 0x401 || (g_planeTable.planes[idx].flags & 0x200)) &&
            objectToScreen(g_planeTable.planes[idx].mapX, g_planeTable.planes[idx].mapY, &screenX, &screenY)) {
            blitSprite(screenX - 1, screenY - 1, 0xb0, 0, 4, 4, 0);
        }
    }
    for (idx = 0; idx < 2; idx++) {
        if (!(g_playerPlaneFlags & (0x4000 >> idx)) &&
            objectToScreen(waypoints[idx + 1].mapX, waypoints[idx + 1].mapY, &screenX, &screenY)) {
            blitSprite(screenX - 1, screenY - 1, 0xa8, 0, 4, 4, 0);
        }
    }
}

void redrawTacMap(int centerX, int centerY) {
    g_mapMode = 0;
    if (g_hudVisible == 0) {
        return;
    }
    renderTacMapContent(centerX, centerY);
    gfx_captureToImage(g_eg2dBacking, *g_pageFront, 24, 112, 24, 112, 72, 56);
    restoreScopePanel();
    resetSimObjectLocks();
}

/* Per-frame tac-map render for renderers that do NOT retain the 2D overlay
 * (the GL backend rebuilds its native vector/quad stream every present). The
 * software backend keeps the cached-into-backing model instead (redrawTacMap +
 * the per-frame marker patch in updateFrame); this path re-emits the whole map —
 * terrain fills/lines as native polygons/vectors, all markers as textured quads —
 * into the current frame's stream, so it stays crisp at the window resolution
 * without touching g_eg2dBacking. Called from renderFrame inside the vector
 * frame; the player marker is drawn here (no restore-from-backing patch). */
void renderTacMapOverlay(void) {
    int16 sx, sy;
    if (g_hudVisible == 0 || g_mapMode != 0) {
        return;
    }
    renderTacMapContent(g_viewX_, g_viewY_);
    if (objectToScreen(g_viewX_, g_viewY_, &sx, &sy)) {
        blitSprite(sx - 1, sy - 1, ((g_ourHead + 0x1000) >> 0xd & 7) * 4 + 164, 4, 4, 4, 0);
    }
}

// ==== seg000:0x9875 ====
void zoomIn(void) {
    if (g_viewMode & 0x80) {
        g_externalCamDist--;
    } else {
        if (g_mapMode == 0 && g_mapZoomLevel < 9) {
            g_mapZoomLevel++;
            redrawTacMap(g_viewX_, g_viewY_);
        }
        if (g_mapMode == 1) {
            g_radarScopeRange++;
        }
    }
}

// ==== seg000:0x98b1 ====
void zoomOut(void) {
    if (g_viewMode & 0x80) {
        g_externalCamDist++;
    } else {
        if (g_mapMode == 0 && g_mapZoomLevel > 2) {
            g_mapZoomLevel--;
            redrawTacMap(g_viewX_, g_viewY_);
        }
        if (g_mapMode == 1 && g_radarScopeRange != 0) {
            g_radarScopeRange--;
        }
    }
}

// ==== seg000:0x98fa ====
int mapXToScreen(int mapX) {
    return ((mapX - g_mapCenterX) >> (10 - g_mapZoomLevel)) + 60;
}

// ==== seg000:0x9915 ====
int mapYToScreen(int mapY) {
    return (((mapY - g_mapCenterY) >> (10 - g_mapZoomLevel)) * 3 >> 1 >> 1) + 140;
}

// ==== seg000:0x993a ====
int plotMapObject(int mapX, int mapY, int color, int big) {
    int screenX;
    int screenY;
    if (g_mapMode != 0 || g_hudVisible == 0) {
        return 0;
    }
    screenX = mapXToScreen(mapX);
    screenY = mapYToScreen(mapY);
    if (color != -1 && screenX >= g_scopeClipLeft && screenX < g_scopeClipRight - 1 && screenY >= g_scopeClipTop && screenY < g_scopeClipBottom - 1) {
        drawMapPoint(screenX, screenY, color);
        if (big != 0) {
            drawMapPoint(screenX + 1, screenY, color);
            drawMapPoint(screenX, screenY + 1, color);
            drawMapPoint(screenX + 1, screenY + 1, color);
        }
        return 0;
    } else {
        return 1;
    }
}

// ==== seg000:0x99ec ====
int objectToScreen(int mapX, int mapY, int16 *outScreenX, int16 *outScreenY) {
    if (g_hudVisible == 0) {
        return 0;
    }
    *outScreenX = mapXToScreen(mapX);
    *outScreenY = mapYToScreen(mapY);
    if (g_scopeClipLeft < *outScreenX && g_scopeClipRight - 1 > *outScreenX &&
        g_scopeClipTop < *outScreenY && g_scopeClipBottom - 1 > *outScreenY) {
        return 1;
    } else {
        return 0;
    }
}

// ==== seg000:0x9a4d ====
extern int mapXToScreen(int);
extern int mapYToScreen(int);

int readMapPixelColor(int mapX, int mapY) {
    int screenX;
    int screenY;
    int color;
    if (g_mapMode != 0) return 0;
    screenX = mapXToScreen(mapX);
    screenY = mapYToScreen(mapY);
    screenX = clampRange(screenX, g_scopeClipLeft, g_scopeClipRight);
    screenY = clampRange(screenY, g_scopeClipTop, g_scopeClipBottom);
    color = -1;
    if (screenX > g_scopeClipLeft && screenX < g_scopeClipRight && screenY > g_scopeClipTop && screenY < g_scopeClipBottom) {
        color = readScreenPixel(screenX, screenY);
    }
    return color;
}

// ==== seg000:0x9adb ====
void drawMapRangeArc(int centerX, int centerY, int radius, int color, int connectLines, int startAngle, int endAngle) {
    int angleFixed;
    int x;
    int angle;
    int prevX;
    int y;
    int e;
    int prevY;

    if (endAngle < startAngle) {
        startAngle += 0x100;
    }
    setDrawColor(color);
    for (angle = startAngle; angle <= endAngle; angle += 0x10) {
        angleFixed = *(unsigned char *)&angle << 8;
        x = sinMul(angleFixed, radius) + centerX;
        y = centerY - cosMul(angleFixed, radius);
        if ((unsigned)x > 0xC000u) {
            x = 0;
        }
        if ((unsigned)y > 0xC000u) {
            y = 0;
        }
        if (angle != startAngle && connectLines != 0) {
            drawMapLine(x, y, prevX, prevY);
        } else {
            plotMapObject(x, y, color, 0);
        }
        prevX = x;
        prevY = y;
    }
}

// ==== seg000:0x9b98 ====
void drawMapLine(int x1, int y1, int x2, int y2) {
    drawClippedLineRegion(mapXToScreen(x1), mapYToScreen(y1), mapXToScreen(x2), mapYToScreen(y2), g_scopeClipLeft, g_scopeClipRight, g_scopeClipTop, g_scopeClipBottom, 1);
}

// ==== seg000:0x9be1 ====
void drawFullscreenLine(int x1, int y1, int x2, int y2) {
    drawClippedLineRegion(x1, y1, x2, y2, 0, 319, 0, 199, 1);
}

// ==== seg000:0x9c0c ====
void drawViewportLine(int x1, int y1, int x2, int y2) {
    int height, width;

    width = g_pageFront[10] - g_pageFront[9] + 1;
    height = g_pageFront[8] - g_pageFront[7] + 1;
    gfx_setBlitOffset(gfx_calcRowAddr(g_pageFront[9], g_pageFront[7]));
    g_clipMaxX = width - 1;
    g_clipMaxY = height - 1;
    gfx_setColor(g_pageFront[2]);
    g_lineX1 = x1;
    g_lineY1 = y1;
    g_lineX2 = x2;
    g_lineY2 = y2;
    drawClipLineGlobal();
    gfx_nop23();
}

// ==== seg000:0x9c84 ====
void drawClippedLineRegion(int x1, int y1, int x2, int y2, int clipLeft, int clipRight, int clipTop, int clipBottom, int drawBothPages) {
    int height, width;

    width = clipRight - clipLeft + 1;
    height = clipBottom - clipTop + 1;
    gfx_setBlitOffset(gfx_calcRowAddr(clipLeft, clipTop));
    g_clipMaxX = width - 1;
    g_clipMaxY = height - 1;
    gfx_setColor(g_pageFront[2]);
    g_lineX1 = x1 - clipLeft;
    g_lineY1 = y1 - clipTop;
    g_lineX2 = x2 - clipLeft;
    g_lineY2 = y2 - clipTop;
    drawClipLineGlobal();
    gfx_nop23();
    (void)drawBothPages; /* single back buffer, so there is no second page */
    g_clipMaxX = 319;
    g_clipMaxY = 199;
    gfx_setBlitOffset(0);
}

// ==== seg000:0x9d86 ====
void drawScreenLineOnePage(int x1, int y1, int x2, int y2) {
    drawClippedLineRegion(x1, y1, x2, y2, 0, 319, 0, 199, 0);
}

// ==== seg000:0x9db0 ====
void drawHudViewLine(int x1, int y1, int x2, int y2) {
    if (g_halfScaleRender != 0) {
        if (gameData->unk4 < 2) {
            drawViewportLine(x1, y1, x2, y2);
        } else {
            drawClippedLineRegion(x1, y1, x2, y2, 104, 216, 62, 96, 0);
        }
    } else if (g_missionStatus != 0) {
        drawClippedLineRegion(x1, y1, x2, y2, 48, 271, 15, 96, 0);
    } else {
        drawViewportLine(x1, y1, x2, y2);
    }
}

/* Fractional-endpoint counterpart of drawHudViewLine for the GL native-res overlay.
 * Picks the same clip region drawHudViewLine would (as a half-open scissor rect) and
 * submits the segment with float 320-space endpoints via r2d_submitScopeLine, so HUD
 * geometry (the target box) keeps its sub-pixel position instead of snapping to the
 * 320x200 grid. Only meaningful while a GL vector frame is active; the software
 * backend stays on the integer drawHudViewLine path (see drawTargetBoxF). */
void drawHudViewLineF(float x1, float y1, float x2, float y2) {
    int l, t, r, b;
    if (g_halfScaleRender != 0) {
        if (gameData->unk4 < 2) {
            l = g_pageFront[9];
            t = g_pageFront[7];
            r = g_pageFront[10] + 1;
            b = g_pageFront[8] + 1;
        } else {
            l = 104;
            t = 62;
            r = 217;
            b = 97;
        }
    } else if (g_missionStatus != 0) {
        l = 48;
        t = 15;
        r = 272;
        b = 97;
    } else {
        l = g_pageFront[9];
        t = g_pageFront[7];
        r = g_pageFront[10] + 1;
        b = g_pageFront[8] + 1;
    }
    r2d_submitScopeLine(x1, y1, x2, y2, g_pageFront[2], l, t, r, b, 1.0f);
}

// ==== seg000:0x9e44 ====
void setDrawColor(int color) {
    g_pageFront[2] = color;
}

// ==== seg000:0x9e5d ====
void fillRectBoth(int x1, int y1, int x2, int y2) {
    fillSpanRect(g_pageFront, x1, y1, x2, y2);
}

// ==== seg000:0x9e94 ====
void drawColorPoint(int screenX, int screenY, int color) {
    setDrawColor(color);
    drawFullscreenLine(screenX, screenY, screenX, screenY);
}

// ==== seg000:0x9ea0 ====
void drawMapPoint(int x, int y, int color) {
    setDrawColor(color);
    drawFullscreenLine(x, y, x, y);
}

// ==== seg000:0x9eb6 ====
void switchIndicatorColor(int indicatorIdx, int color) {
    if (g_hudVisible == 0) goto done;
    if (*(g_tacmapIndicators + indicatorIdx * 5 + 7) != color) {
        gfx_switchColor(g_pageFront, *(g_tacmapIndicators + indicatorIdx * 5 + 3), *(g_tacmapIndicators + indicatorIdx * 5 + 4), *(g_tacmapIndicators + indicatorIdx * 5 + 5), *(g_tacmapIndicators + indicatorIdx * 5 + 6), *(g_tacmapIndicators + indicatorIdx * 5 + 7), color);
        *(g_tacmapIndicators + indicatorIdx * 5 + 7) = color;
    }
done:;
}

// ==== seg000:0x9fad ====
void drawPanelText(int panel, const char *text, int color) {
    fillPanelBox(panel, color);
    drawCenteredLabelBox(panel, text);
}

// ==== seg000:0x9fcc ====
void fillPanelBox(int panelId, int color) {
    setDrawColor(color);
    if (panelId == 1) {
        fillRectBoth(24, 112, 96, 168);
    }
    if (panelId == 2) {
        fillRectBoth(120, 104, 199, 175);
    }
    if (panelId == 3) {
        fillRectBoth(232, 128, 304, 184);
    }
}

// ==== seg000:0xa0cb ====
void drawStringBothPages(const char *text, int screenX, int screenY, int color) {
    egDrawStringCentered(g_pageFront, text, screenX, screenY, color);
}

// ==== seg000:0xa0fe ====
void drawStringActivePage(const char *text, int screenX, int screenY, int color) {
    egDrawStringCentered(g_pageFront, text, screenX, screenY, color);
}

// ==== seg000:0xa13a ====
void egDrawStringCentered(int16 *strStruct, const char *text, int screenX, int screenY, int color) {
    char buf[256];
    strStruct[6] = 0;
    strStruct[4] = screenX;
    strStruct[5] = screenY;
    strStruct[2] = color;
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    gfx_drawString(strStruct, strupr(buf));
}

// ==== seg000:0xa183 ====
void drawNumber(int value, int x, int y, int color) {
    char buf[20];
    itoa(value, buf, 10);
    drawStringBothPages(buf, x, y, color);
}

// ==== seg000:0xa1b1 ====
/* Recover the tac-scope terrain colour under a moving blip so callers can erase
 * the blip by re-plotting it (see plotMapObject/targetLock). The DOS original read
 * the live VGA page via INT 10h AH=0Dh; we sample g_eg2dBacking, the clean cached
 * scope image (redrawTacMap captures it before per-frame blips are drawn), whose
 * 320x200 pixels sit at their screen coords over the scope-clip region (24..96,
 * 112..168). Backend-agnostic: on GL this erase-plot is never presented, but the
 * sample stays valid. */
int readScreenPixel(int screenX, int screenY) {
    return gfx_readImagePixel(g_eg2dBacking, screenX, screenY);
}

// ==== seg000:0xa1e4 ====
void hudMessage(const char *src) {
    strcpy(tempString, src);
    g_hudMsgTimer = g_frameRateScaling * 3;
}

// ==== seg000:0xa204 ====
void setTimedMessage(char *message) {
    strcpy(string_3C04A, message);
    g_dirMsgTimer = g_frameRateScaling * 3;
}

// ==== seg000:0xa224 ====
int missileTargetCompat(int weaponType, int objIdx) {
    return (int)(char)g_targetCompatTable[weaponType * 13 + ((int)(char)g_shapeTargetCategory[g_planeTable.planes[objIdx].nameIndex & 0x7f] & 0xf)];
}
