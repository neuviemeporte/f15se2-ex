/* Random number utilities */
#include "strand.h"
#include "shared/common.h"
#include "shared/blackbox.h"

#include <dos.h>

void seedRandom() {
    if (blackbox_randomActive()) {
        blackbox_seedConfiguredRandom();
        return;
    }
    srand(getTimeOfDay());
}

void gameSrand(uint32 seed) {
    if (blackbox_randomActive()) {
        blackbox_seedRandom(seed);
    } else {
        srand(seed);
    }
}

void gameSrandFromClock(int16 *seed) {
    if (blackbox_randomActive()) {
        int16 clockSeed = blackbox_recording() ? (int16)getTimeOfDay() : 0;
        uint32 externalSeed = (uint32)clockSeed;
        *seed = (int16)blackbox_seedExternalRandom(externalSeed);
    } else {
        *seed = getTimeOfDay();
        srand(*seed);
    }
}

int gameRand(void) {
    return blackbox_randomActive() ? blackbox_rand15() : rand();
}

int gameRand15(void) {
    return blackbox_randomActive() ? blackbox_rand15() : (rand() & 0x7fff);
}

int16 randMul(uint16 arg) {
    /* DOS rand() is 15-bit (RAND_MAX 0x7fff); mask to match so the >>15 scaling yields [0, arg). */
    return (gameRand15() * (int32)arg) >> 0xf;
}
