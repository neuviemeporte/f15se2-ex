#ifndef F15_SE2_BLACKBOX_DIAG_H
#define F15_SE2_BLACKBOX_DIAG_H

#include "inttype.h"
#include "r3d.h"

#ifdef __cplusplus
extern "C" {
#endif

void blackbox_diagReset(void);
int blackbox_diagParseReplayLine(const char *line);
int blackbox_diagValidateReplay(void);
void blackbox_diagShutdown(int pausedForInspection);
void blackbox_diagSetRenderCapture(int enabled);
void blackbox_diagSetDumpTick(uint32 tick);
void blackbox_diagOnTick(void);

/* Generic marker for future focused instrumentation. The default flight hook
 * already emits view/target/projectile/mission transitions automatically. */
void blackbox_diagMarker(const char *name, int32 a, int32 b, int32 c);
void blackbox_diagCaptureSimStep(void);

void blackbox_diagBeginRenderFrame(void);
void blackbox_diagRenderBeginScene(const R3DScene *scene);
void blackbox_diagRenderSubmit(const R3DSubmit *submit);
void blackbox_diagRenderLine(const R3DLine *line);
void blackbox_diagRenderEndScene(void);

/* Writes a human-readable snapshot. Ctrl+F10 calls the automatic-name variant. */
int blackbox_diagWriteDump(const char *path);
void blackbox_diagWriteAutomaticDump(void);

#ifdef __cplusplus
}
#endif

#endif /* F15_SE2_BLACKBOX_DIAG_H */
