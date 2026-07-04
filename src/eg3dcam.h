#ifndef F15_SE2_EG3DCAM
#define F15_SE2_EG3DCAM
/* public interface of eg3dcam.c */

void setViewRotation(int, int, int);
void setViewPosition(int viewX, int viewY, int viewZ);
void setViewPositionFrac(int fracX, int fracY, int fracZ);

#endif /* F15_SE2_EG3DCAM */
