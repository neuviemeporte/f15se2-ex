#ifndef F15_SE2_EG3DCAM
#define F15_SE2_EG3DCAM
#include "inttype.h"
/* public interface of eg3dcam.c */

void setViewRotation(int16, int16, int16);
void setViewPosition(int16 viewX, int16 viewY, int16 viewZ);
void setViewPositionFrac(int fracX, int fracY, int fracZ);

#endif /* F15_SE2_EG3DCAM */
