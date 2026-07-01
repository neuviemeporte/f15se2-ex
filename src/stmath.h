#ifndef F15_SE2_STMATH
#define F15_SE2_STMATH

#include "inttype.h"

int approxDistance(int dx, int dy);
int calcBearing(int dx, int dy);
int clampValue(int val, int lo, int hi);
uint32 scaleCoordByLevel(int level, uint32 coord);

#endif /* F15_SE2_STMATH */
