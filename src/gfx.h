#ifndef F15_SE2_GFX
#define F15_SE2_GFX

#define INT_VID_MODESET 0
#define MODE_640_350 0x10

#define PORT_MDA_STATUS 0x3ba
#define PORT_CGA_STATUS 0x3da

/* Bug? Bit 7 does notthing in the MDA status port according to docs */
#define MDA_STATUS_RETRACE 0x80
#define CGA_STATUS_RETRACE 0x8

/* Logical resolutions presented through SDL. */
#define LOGICAL_WIDTH 320
#define LOGICAL_HEIGHT 200
#define HIRES_WIDTH 640
#define HIRES_HEIGHT 350

/* The SDL window and renderer are owned by the graphics layer (gfx_impl.c).
 * gfx_videoInit() creates them; gfx_videoShutdown() tears them down. */
void gfx_videoInit(void);
void gfx_videoShutdown(void);

/* Toggle borderless-desktop fullscreen (bound to Alt+Enter in the input pump). */
void gfx_toggleFullscreen(void);

/* Title-screen hi-res. Asks SDL change resolution to 640x350 and returns whether that took. */
bool video_setHiRes(void);

/* Hi-res (640x350) title surface and its present. picBlit decodes the planar
 * Title640.pic into this surface; gfx_presentHiRes pushes it to the renderer. */
struct SDL_Surface *gfx_getHiResSurface(void);
void gfx_presentHiRes(void);

#endif /* F15_SE2_GFX */
