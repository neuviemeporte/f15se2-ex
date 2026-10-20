#ifndef XR_H
#define XR_H

/*
 * OpenXR VR mode (CMake VR=ON), driven by the GL backend.
 *
 * Flight frames: the GL backend opens an OpenXR frame at the main 3D scene
 * (xr_beginFrame), replays the scene once per eye into the eye swapchains
 * (xr_bindEye / xr_releaseEye), and the present submits the stereo world plus the
 * window as a cockpit quad whose windscreen is transparent (xr_setScreen places it).
 * Every other frame (menus, briefing, debrief) shows the window on a flat virtual
 * screen; the compositor does the head tracking.
 *
 * Every entry point is a no-op when VR is compiled out or no runtime/headset is
 * available, so the game stays a plain flat window.
 */

struct SDL_Window;

#ifdef F15_VR
/* Before the GL window exists: create the OpenXR instance + find the HMD. Nonzero
 * when a runtime and headset are present (the GL backend then skips window MSAA so
 * the back buffer can be blitted into the swapchain). */
int xr_probe(void);
/* With the GL context current: bind the session to it. 0 = stay flat (all torn down). */
int xr_startSession(void);
void xr_shutdown(void);
/* Open this frame's OpenXR frame (blocks in xrWaitFrame, pacing the loop to the
 * headset) and locate the eyes. Returns the number of eyes to render, 0 for none;
 * the frame stays open until xr_presentWindow either way. */
int xr_beginFrame(void);
/* The head's orientation for the open frame (row-major 3x3, seated space: +X right,
 * +Y up, -Z forward). Valid after xr_beginFrame returned eyes. */
void xr_headRotation(float *m);
/* Re-anchor the seated space at the head on the next frame (also automatic when a
 * flight starts). */
void xr_recenter(void);
/* Bind eye `eye`'s framebuffer (colour + depth/stencil) and return its column-major
 * projection (near/far in metres) and LOCAL-space view matrices and pixel size. */
int xr_bindEye(int eye, float nearM, float farM, float *proj, float *view, int *w, int *h);
void xr_releaseEye(int eye);
/* Place the window quad for the open frame (LOCAL metres: centre x/y, distance
 * ahead, width/height); with eyes rendered it is the cockpit over the world. */
void xr_setScreen(float x, float y, float dist, float w, float h);
/* Before the desktop swap: copy the window's back buffer onto the headset screen
 * and submit the frame. Returns 0 once there is no session (never started, or
 * ended by the runtime): the caller paces itself again. */
int xr_presentWindow(struct SDL_Window *win);
/* From the game's wait loops (timerPump): keep the headset fed with the last image
 * while the game isn't presenting. Main thread only (needs the GL context). */
void xr_idle(void);
#else
static inline int xr_probe(void) { return 0; }
static inline int xr_startSession(void) { return 0; }
static inline void xr_shutdown(void) {}
static inline int xr_beginFrame(void) { return 0; }
static inline void xr_headRotation(float *m) { (void)m; }
static inline void xr_recenter(void) {}
static inline int xr_bindEye(int eye, float nearM, float farM, float *proj, float *view, int *w,
                             int *h) {
    (void)eye; (void)nearM; (void)farM; (void)proj; (void)view; (void)w; (void)h;
    return 0;
}
static inline void xr_releaseEye(int eye) { (void)eye; }
static inline void xr_setScreen(float x, float y, float dist, float w, float h) {
    (void)x; (void)y; (void)dist; (void)w; (void)h;
}
static inline int xr_presentWindow(struct SDL_Window *win) { (void)win; return 0; }
static inline void xr_idle(void) {}
#endif

#endif /* XR_H */
