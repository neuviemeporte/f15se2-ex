/* enaward.c — memory/pics/awards, compiled with /Gs */
#include "gfx.h"
#include "log.h"
#include "endata.h"
#include "endcode.h"
#include "enaward.h"
#include "eninput.h"
#include "shared/common.h"
#include <SDL3/SDL.h>

void loadPicFromFile(const char *name, uint16 segment);
void loadPicFromFileAt(const char *name, uint16 segment, int16 off, SDL_IOWhence whence);

void loadPicFromFile(const char *name, uint16 segment) {
    SDL_IOStream *handle;
    handle = openFileWrapper(name, 0);
    decodePicRaw(handle, segment);
    closeFileWrapper(handle);
}

void loadPicFromFileAt(const char *name, uint16 segment, int16 off, SDL_IOWhence whence) {
    SDL_IOStream *handle;
    handle = openFileWrapper(name, 0);
    SDL_SeekIO(handle, off, whence);
    decodePic(handle, segment);
    closeFileWrapper(handle);
}

// 1e78
void showPostMissionAwards(void) {
    int16 idx;
    awardPage[3] = 0;
    if (commData->trainingFlag != 0)
        goto done;
    if (gameData->campaignProgress == 1) {
        gfx_setFadeSteps(3);
        openShowPic("desk.pic", *awardPage);
        drawStringCentered(awardPage, "After ditching three very expensive aircraft,", 36, 179, 250);
        drawStringCentered(awardPage, "you are assigned a desk job.", 36, 188, 250);
        // 1ef4
        awardFont = 4;
        awardColor = 0;
        idx = 0;
        // 1f05
        for (; (textBuf[idx] = gameData->pilotName[idx]) != 0; idx++) {}
        drawStringCentered(awardPage, textBuf, 193, 153, 95);
        awardColor = 7;
        awardFont = 1;
        goto show;
    }
    if (gameData->campaignProgress == 2) {
        gfx_setFadeSteps(2);
        openShowPic("death.pic", *awardPage);
        drawStringCentered(awardPage, "In the wake of the horrible crash,", 36, 173, 250);
        drawStringCentered(awardPage, "your family and friends mourn your loss.", 36, 182, 250);
        goto show;
    }
    // 1fa8
    if (((uint16)gameData->rank < 6) && (promoThresholds[gameData->rank] < gameData->totalScore)) {
        gfx_setFadeSteps(6);
        openShowPic("promo.pic", *awardPage);
        awardColor = 1;
        drawStringCentered(awardPage, "For your consistently successful missions,", 36, 174, 250);
        mystrcpy(textBuf, "you have been promoted to ");
        mystrcat(textBuf, rankNames[++gameData->rank]);
        drawStringCentered(awardPage, textBuf, 36, 183, 250);
        gfx_commitPage();
        gfx_flipPage();
        waitForKeyOrJoy();
    }
medals:
    idx = 4;
    for (; idx >= 0; idx--) {
        if (missionScore > medalThresholds[idx])
            break;
    }
    if (idx < 0)
        goto done;
    if (gameData->medals & (1 << (char)idx))
        goto done;
    gfx_waitRetrace();
    gfx_setFadeSteps(10);
    openShowPic("medal.pic", *awardPage);
    awardColor = 0x0f;
    drawStringCentered(awardPage, "For your outstanding performance, you receive", 36, 174, 250);
    mystrcpy(textBuf, "the ");
    mystrcat(textBuf, medalNames[idx]);
    drawStringCentered(awardPage, textBuf, 36, 183, 250);
    gameData->medals |= (1 << (char)idx);
show:
    gfx_commitPage();
    gfx_flipPage();
    waitForKeyOrJoy();
done:
    clearRect(awardPage, 0, 0, 319, 199);
    gfx_flipPage();
}
