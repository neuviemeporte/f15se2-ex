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
void drawStringAt(int16 *pageNum, const char *string, int x, int y);

/* Optional per-string recorder: when non-NULL, drawStringAt reports every string it
 * draws (final position + current colour/font) so a screen can rebuild its text as a
 * per-frame overlay scene rather than have it bake once into the page. Only the START
 * briefing sets this (to layer live text over the HD room); NULL everywhere else, so
 * END/flight text bakes as before. */
extern void (*g_textRecorder)(const char *str, int x, int y, int color, int font);

/* text layout & number formatting - textfmt.c */
void drawStringCentered(int16 *page, const char *str, int startx, int y, int endx);
int stringWidth(int16 *page, const char *str);
void my_ltoa(int32 value, char *buf);
void my_itoa(int value, char *buf);

/* file/picture wrappers & strcpy - filepic.c */
SDL_IOStream *openFileWrapper(const char *filename, int mode);
void closeFileWrapper(SDL_IOStream *handle);
void mystrcpy(char *dest, const char *source);
void loadPic(const char *filename, int segment);
void openShowPic(const char *filename, int page);
void showPicFile(SDL_IOStream *handle, int pageNum);

/* functions provided by file_io.c / file_*.inc - case-insensitive asset I/O */
SDL_IOStream *openFile(const char *filename, int mode);
SDL_IOStream *createFile(const char *filename, int attr);
void fileClose(SDL_IOStream *handle);
size_t fileRead(void *ptr, size_t size, size_t count, SDL_IOStream *handle);
size_t fileWrite(const void *ptr, size_t size, size_t count, SDL_IOStream *handle);

/* functions provided by miscimpl.c / overlay_dispatch.inc */
void intDispatch(int intNum, uint8 *inRegs, uint8 *outRegs);
void restoreCbreakHandler(void);
void installCBreakHandler(void);
int getTimeOfDay(void);

/* functions provided by timer.c / timer_*.inc */
void setTimerIrqHandler(void);
void restoreTimerIrqHandler(void);
/* Advance the 60 Hz tick counters from the monotonic clock (call while polling
 * a timerCounter); timerYield also sleeps a touch so the wait doesn't peg a core. */
void timerPump(void);
void timerYield(void);

#endif /* COMMON_H */
