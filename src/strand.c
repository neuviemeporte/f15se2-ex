/* Random number utilities */
#include "strand.h"
#include "shared/common.h"

#include <dos.h>

void seedRandom() {
    srand(getTimeOfDay());
}

int16 randMul(uint16 arg) {
    /* DOS rand() is 15-bit (RAND_MAX 0x7fff); mask to match so the >>15 scaling yields [0, arg). */
    return ((rand() & 0x7fff) * (int32)arg) >> 0xf;
}
