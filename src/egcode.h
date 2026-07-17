#ifndef F15_SE2_EGCODE
#define F15_SE2_EGCODE
/* assembly routines (egcode.asm/egseg*.asm) called from C */
#include "inttype.h"
#include <dos.h>
#include "egtypes.h"
#include <stddef.h>

typedef struct SDL_IOStream SDL_IOStream;

int loadF15DgtlBin();
void setupDac();
int fixedMulQ14(int a, int b);
int16 cosine(int16 angle);
int16 sine(int16 angle);
void restoreCbreakHandler();
void runGameLoop();
void gameMainLoop();
void advanceFrameTick();
int drawCenteredLabelBox(int panel, const char *text);
SDL_IOStream *createFile(const char *path, int attr);
void picBlit(SDL_IOStream *handle, int unk);
void shiftLongLeftInPlace(int count, long *ptr);
void shiftLongRightInPlace(int count, long *ptr);
int far drawPolygonOutline(int fillColor, int pointCount, int *points, int edgeColor);
int far drawFlatHorizon(int);
void storeObjTransformByOpcode();
int far advanceModelPointerLod();
int far renderSortedListFar();
int far rotatePoint3dFar();
void rotatePoint3d();
int far transformModelVerticesFar();
int far projectModelEdgesFar();
int far buildRotationMatrixFar(int16 *matrix, int angleX, int angleY, int angleZ);
int far multiplyMatrix3x3Far(const int16 *matA, const int16 *matB, int16 *result);
int far r3d_objTransformFar(char far *model, int yaw, int pitch, int roll,
                            int posX, int posY, int posZ,
                            int16 *combined, long *camBase, long *camX, long *camY,
                            int *shade);
/* World point (view-relative, transformAndCullObject arg order) -> scene camera
 * space; the 3D line primitive (tracers / explosion sparks) transforms each
 * endpoint with this. */
void far r3d_worldPointToCameraFar(int relY, int relZ, int relX,
                                   long *baseX, long *camX, long *camY);
/* Queue a camera-space 3D line into the software depth-sorted line list (drawn,
 * occluded + interleaved with objects, by renderSortedListFar). */
void far r3d_submitLineFar(long baseXA, long camXA, long camYA,
                           long baseXB, long camXB, long camYB, int color);
/* Widen the object frustum cull (transformAndCullObject) to a wider-than-4:3 view
 * cone, so widescreen 3D fetches the peripheral models the central frustum would
 * reject. The X/Y half-extents are scaled by numX/denX and numY/denY (window vs
 * the centred 4:3 sub-rect). Set 1,1,1,1 to disable (the software path default).
 * Only the angular cull is widened; the near/far depth and max-distance gates are
 * unchanged. The GL backend sets this per scene in gl_beginScene. */
void r3d_setObjCullWiden(int numX, int denX, int numY, int denY);
int far drawModelDisplayList();
int16 far fillSpanRect(const int16 *dst, int16 left, int16 top, int16 right, int16 bottom);
int far fillSpanRectImmediate(const int16 *dst, int left, int top, int right, int bottom);
int far drawClipLineGlobal();
int far flushSpanDirtyRect();
int far resetScanlineSpans();
int far clipAndRasterizeEdge();
void __far setupInstrumentLayoutFar();
void __far drawInstrumentGaugesFar();
int far initJoystickCalibration();
void seedJoystickBaseline();
int far readCalibratedJoystick();
void readJoystickHardware();
void computeJoystickAxis();
int far restoreJoystickData(uint8 FAR *ptr);
void far copyJoystickData(uint8 FAR *ptr);
int far setInt9Handler();
int far restoreInt9Handler();
int int9Handler();
extern long _aNlmul(long, long);

void installCBreakHandler();
void setTimerIrqHandler();
void restoreTimerIrqHandler();
/* per-tick game work + its registration hook (shared/timer.c + egsys.c); the
 * verify ASM build runs egcode.asm's own timer ISR instead, so this is NO_ASM. */
void setTimerTickHook(void(far *fn)(void));
void far egAdvanceFrameTick(void);
/* Advance the 60 Hz tick counters from the monotonic clock (call while polling
 * a tick counter); timerYield also sleeps a touch so the wait doesn't peg a core. */
void timerPump(void);
void timerYield(void);
uint64 timerNowNs(void);
int getTimeOfDay();
SDL_IOStream *openFile(const char *path, int mode);
int findReplacementAssetPath(const char *legacyFilename, const char *modernExt,
                             char *outPath, size_t outPathSize);
int findReplacementShapeModelPath(const char *containerLegacyFilename, int shapeId,
                                  const char *modernExt, char *outPath,
                                  size_t outPathSize);
void fileClose(SDL_IOStream *handle);
size_t fileRead(void *ptr, size_t size, size_t count, SDL_IOStream *handle);
size_t fileWrite(const void *ptr, size_t size, size_t count, SDL_IOStream *handle);

void far projectSceneObject(char far *model, int yaw, int pitch, int roll, int posX, int posY, int posZ);
void far projectSceneShadow(char far *model, int yaw, int posX, int posY, int posZ);

#endif /* F15_SE2_EGCODE */
