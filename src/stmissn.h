#ifndef F15_SE2_STMISSN
#define F15_SE2_STMISSN
/* mission select/decode (stmissn.c) */
#include "inttype.h"

void clearKeybuf(void);
void waitMdaCgaStatus(int16 iter);
void drawLine(const int16 *pageNum, int x1, int y1, int x2, int y2, int color);
void showPic640(const char *filename);
void missionSelect(void);
int16 askRepeatMission(void);
void checkDiskA(void);
void missionDecode(void);
void printMission(void);
int16 pollMenuInput();

/* End the HD briefing scene: stop recording text, drop the recorder + repaint hook.
 * Call once the briefing board is no longer shown (START handing off to EGAME). */
void briefingSceneEnd(void);

#endif /* F15_SE2_STMISSN */
