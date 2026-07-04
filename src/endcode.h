#ifndef F15_SE2_ENDCODE
#define F15_SE2_ENDCODE
#include "inttype.h"
#include <dos.h>
/* assembly routines in endcode.asm called from C */

typedef struct SDL_IOStream SDL_IOStream;

void decodePic(SDL_IOStream *handle, int16 segment);
void dos_printstring(const char *str);
SDL_IOStream *createFile(const char *name, int16 mode);
extern void far pollJoystick(void);
void drawLineWrapper(void);
void clearRect(int16 *page, int16 y1, int16 x1, int16 x2, int16 y2);
void mystrcat(char *dst, const char *src);
void decodePicRaw(SDL_IOStream *handle, uint16 segment);
extern void far copyJoystickData(uint8 far *data);

#endif /* F15_SE2_ENDCODE */
