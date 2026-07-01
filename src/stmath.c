#include "stmath.h"
#include "const.h"

#include <stdlib.h>

int approxDistance(int dx, int dy) {
    long dist;
    dx = abs(dx);
    dy = abs(dy);
    dist = (dx > dy) ? (long)(dy >> 1) + (long)dx : (long)(dx >> 1) + (long)dy;
    if (dist > 0x7fff) {
        dist = 0x7fff;
    }
    return dist;
}

int calcBearing(int dx, int dy) {
    int16 angle, result;
    int32 ratio;
    int16 divisor, swapped, quotient;
    if (dx == 0) {
        return (dy > 0) ? 0 : BEARING_SOUTH;
    }
    if (dy == 0) {
        return (dx > 0) ? BEARING_EAST : BEARING_WEST;
    }
    if (abs(dx) > abs(dy)) {
        ratio = (int32)abs(dy) << 0xe;
        divisor = abs(dx);
        swapped = 1;
    } else {
        ratio = (int32)abs(dx) << 0xe;
        divisor = abs(dy);
        swapped = 0;
    }
    quotient = ratio / (int32)divisor;
    angle = ((0x2800 - (((int32)abs((0x1333 - quotient)) * (int32)0xb00) >> 0xe)) * (int32)quotient) >> 0xe;
    if (dx > 0) {
        if (dy > 0) {
            result = swapped != 0 ? BEARING_EAST - angle : angle;
        } else {
            result = (swapped != 0) ? angle + BEARING_EAST : BEARING_SOUTH - angle;
        }
    } else {
        if (dy > 0) {
            result = (swapped != 0) ? angle + BEARING_WEST : -angle;
        } else {
            result = (swapped != 0) ? BEARING_WEST - angle : angle + BEARING_SOUTH;
        }
    }
    return result;
}

int clampValue(int val, int lo, int hi) {
    if (val > hi) return hi;
    if (val >= lo) return val;
    if (val > -16384) return lo;
    return hi;
}

uint32 scaleCoordByLevel(int level, uint32 coord) {
    switch (level) {
    case 4:
        return coord >> 6;
    case 3:
        return coord >> 4;
    case 2:
        return coord >> 2;
    case 1:
        return coord;
    default: // case 0
        return coord << 1;
    }
}
