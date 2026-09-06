#ifndef F15_SE2_STRAND
#define F15_SE2_STRAND
/* RNG (strand.c) */
#include "inttype.h"

void seedRandom();
void gameSrand(uint32 seed);
void gameSrandFromClock(int16 *seed);
int gameRand(void);
int gameRand15(void);
int16 randMul(uint16);

#endif /* F15_SE2_STRAND */
