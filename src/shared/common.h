/*
 * common.h - Declarations for functions shared between start.exe and end.exe
 */
#ifndef COMMON_H
#define COMMON_H

#include "../inttype.h"
#include "../const.h"
#include <stddef.h>

typedef struct SDL_IOStream SDL_IOStream;

/* program teardown - cleanup.c */
void cleanup(void);

/* string drawing - drawstr.c */
void drawStringAt(int16 *pageNum, const char *string, int16 x, int16 y);

/* text layout & number formatting - textfmt.c */
void drawStringCentered(int16 *page, const char *str, int16 startx, int16 y, int16 endx);
int16 stringWidth(int16 *page, const char *str);
void my_ltoa(int32 value, char *buf);
void my_itoa(int16 value, char *buf);

/* file/picture wrappers & strcpy - filepic.c */
SDL_IOStream *openFileWrapper(const char *filename, int16 mode);
void closeFileWrapper(SDL_IOStream *handle);
void mystrcpy(char *dest, const char *source);
void loadPic(const char *filename, uint16 segment);
void openShowPic(const char *filename, int16 page);
void showPicFile(SDL_IOStream *handle, int16 pageNum);

/* functions provided by file_io.c / file_*.inc - case-insensitive asset I/O */
SDL_IOStream *openFile(const char *filename, int16 mode);
SDL_IOStream *createFile(const char *filename, int attr);
void fileClose(SDL_IOStream *handle);
size_t fileRead(void *ptr, size_t size, size_t count, SDL_IOStream *handle);
size_t fileWrite(const void *ptr, size_t size, size_t count, SDL_IOStream *handle);

/* functions provided by miscimpl.c / overlay_dispatch.inc */
void intDispatch(int16 intNum, uint8 *inRegs, uint8 *outRegs);
void restoreCbreakHandler(void);
void installCBreakHandler(void);
int16 getTimeOfDay(void);

/* functions provided by timer.c / timer_*.inc */
void setTimerIrqHandler(void);
void restoreTimerIrqHandler(void);
/* Advance the 60 Hz tick counters from the monotonic clock (call while polling
 * a timerCounter); timerYield also sleeps a touch so the wait doesn't peg a core. */
void timerPump(void);
void timerYield(void);

#endif /* COMMON_H */
