#ifndef F15_SE2_EGCODE
#define F15_SE2_EGCODE
/* assembly routines (egcode.asm/egseg*.asm) called from C */
#include "inttype.h"
#include "dos_compat.h"
#include "egtypes.h"
#include <stddef.h>

typedef struct SDL_IOStream SDL_IOStream;

int16 loadF15DgtlBin();
void setupDac();
int16 fixedMulQ14(int16 a, int16 b);
int16 cosine(int16 angle);
int16 sine(int16 angle);
void restoreCbreakHandler();
void runGameLoop();
void gameMainLoop();
void advanceFrameTick();
int16 drawCenteredLabelBox(int16 panel, const char *text);
SDL_IOStream *createFile(const char *path, int16 attr);
void picBlit(SDL_IOStream *handle, int16 unk);
void shiftLongLeftInPlace(int16 count, int32 *ptr);
void shiftLongRightInPlace(int16 count, int32 *ptr);
int16 FAR drawPolygonOutline(int16 fillColor, int16 pointCount, int16 *points, int16 edgeColor);
int16 FAR drawFlatHorizon(int16);
void storeObjTransformByOpcode();
int16 FAR advanceModelPointerLod();
int16 FAR renderSortedListFar();
int16 FAR rotatePoint3dFar();
void rotatePoint3d();
int16 FAR transformModelVerticesFar();
int16 FAR projectModelEdgesFar();
int16 FAR buildRotationMatrixFar(int16 *matrix, int16 angleX, int16 angleY, int16 angleZ);
int16 FAR multiplyMatrix3x3Far(const int16 *matA, const int16 *matB, int16 *result);
int FAR r3d_objTransformFar(char far *model, int yaw, int pitch, int roll,
                            int posX, int posY, int posZ,
                            int16 *combined, long *camBase, long *camX, long *camY,
                            int *shade);
/* World point (view-relative, transformAndCullObject arg order) -> scene camera
 * space; the 3D line primitive (tracers / explosion sparks) transforms each
 * endpoint with this. */
void FAR r3d_worldPointToCameraFar(int relY, int relZ, int relX,
                                   long *baseX, long *camX, long *camY);
/* Queue a camera-space 3D line into the software depth-sorted line list (drawn,
 * occluded + interleaved with objects, by renderSortedListFar). */
void FAR r3d_submitLineFar(long baseXA, long camXA, long camYA,
                           long baseXB, long camXB, long camYB, int color);
/* Widen the object frustum cull (transformAndCullObject) to a wider-than-4:3 view
 * cone, so widescreen 3D fetches the peripheral models the central frustum would
 * reject. The X/Y half-extents are scaled by numX/denX and numY/denY (window vs
 * the centred 4:3 sub-rect). Set 1,1,1,1 to disable (the software path default).
 * Only the angular cull is widened; the near/far depth and max-distance gates are
 * unchanged. The GL backend sets this per scene in gl_beginScene. */
void r3d_setObjCullWiden(int numX, int denX, int numY, int denY);
int16 FAR drawModelDisplayList();
int16 FAR fillSpanRect(const int16 *dst, int16 left, int16 top, int16 right, int16 bottom);
int16 FAR drawClipLineGlobal();
int16 FAR flushSpanDirtyRect();
int16 FAR resetScanlineSpans();
int16 FAR clipAndRasterizeEdge();
void FAR setupInstrumentLayoutFar();
void FAR drawInstrumentGaugesFar();
int16 FAR initJoystickCalibration();
void seedJoystickBaseline();
int16 FAR readCalibratedJoystick();
void readJoystickHardware();
void computeJoystickAxis();
int16 FAR restoreJoystickData(uint8 FAR *ptr);
void FAR copyJoystickData(uint8 FAR *ptr);
int16 FAR setInt9Handler();
int16 FAR restoreInt9Handler();
int16 int9Handler();
extern int32 _aNlmul(int32, int32);

void installCBreakHandler();
void setTimerIrqHandler();
void restoreTimerIrqHandler();
/* per-tick game work + its registration hook (shared/timer.c + egsys.c); the
 * verify ASM build runs egcode.asm's own timer ISR instead, so this is NO_ASM. */
void setTimerTickHook(void(FAR *fn)(void));
void FAR egAdvanceFrameTick(void);
/* Advance the 60 Hz tick counters from the monotonic clock (call while polling
 * a tick counter); timerYield also sleeps a touch so the wait doesn't peg a core. */
void timerPump(void);
void timerYield(void);
uint64 timerNowNs(void);
int16 getTimeOfDay();
SDL_IOStream *__cdecl openFile(const char *path, int16 mode);
void fileClose(SDL_IOStream *handle);
size_t fileRead(void *ptr, size_t size, size_t count, SDL_IOStream *handle);
size_t fileWrite(const void *ptr, size_t size, size_t count, SDL_IOStream *handle);

void FAR projectSceneObject(char FAR *model, int16 yaw, int16 pitch, int16 roll, int16 posX, int16 posY, int16 posZ);

#endif /* F15_SE2_EGCODE */
