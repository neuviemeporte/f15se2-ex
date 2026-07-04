/* enfile.c — file I/O helpers, compiled with /Gs */
#include "log.h"
#include "endata.h"
#include "endcode.h"
#include "enfile.h"
#include "shared/common.h"

void srandInit(int seed) {
    randSeed = seed;
    randState = 0;
}
