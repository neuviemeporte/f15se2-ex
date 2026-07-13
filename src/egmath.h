#ifndef F15_SE2_EGMATH
#define F15_SE2_EGMATH
/* public interface of egmath.c */

#include "inttype.h"

void load15Flt3d3();
void drawWorldObject(int16 shapeId, int32 worldX, int32 worldY, int16 altitude, int16 objYaw, int16 objPitch, int16 objRoll, int16 scaleShift);
void drawAircraftShadow(int16 shapeId, int32 worldX, int32 worldY, int16 groundAltitude, int16 objYaw, int16 objPitch, int16 objRoll, int16 scaleShift);
void drawWorldLine(long worldX1, long worldY1, int alt1, long worldX2, long worldY2, int alt2, int color);
void drawTargetView(int shapeId, int32 worldX, int32 worldY, int altitude, int objYaw, int objPitch, int objRoll, int mode, int shift);
int shapeDataOffset(int shapeId);
int16 clampRange(int16 value, int16 minVal, int16 maxVal);
int egClampValue(int value, int minVal, int maxVal);
int16 computeBearing(int16 deltaX, int16 deltaY);
int16 cosMul(int16 angle, int16 value);
long sinMulQ8(int angle, int value);
long cosMulQ8(int angle, int value);
int16 signOf(int16 value);
void seedRng(void);
int16 readAxisInput(int16 axisIdx);

#endif /* F15_SE2_EGMATH */
