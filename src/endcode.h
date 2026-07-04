#ifndef F15_SE2_ENDCODE
#define F15_SE2_ENDCODE
/* assembly routines in endcode.asm called from C */
#include "inttype.h"
#include <dos.h>

typedef struct SDL_IOStream SDL_IOStream;

void decodePic(SDL_IOStream *handle, int segment);
void dos_printstring(const char *str);
SDL_IOStream *createFile(const char *name, int mode);
extern void far pollJoystick(void);
void drawLineWrapper(void);
void clearRect(int16 *page, int y1, int x1, int x2, int y2);
void mystrcat(char *dst, const char *src);
void decodePicRaw(SDL_IOStream *handle, int segment);
extern void far copyJoystickData(uint8 far *data);

#endif /* F15_SE2_ENDCODE */
