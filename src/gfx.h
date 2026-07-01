#ifndef F15_SE2_GFX
#define F15_SE2_GFX

#include "inttype.h"
#include "pointers.h"

#define INT_VID_MODESET 0
#define MODE_640_350 0x10

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

/* Re-present the current visible frame. The input pump calls this on window
 * events (resize / fullscreen toggle / expose) so the image is redrawn even on
 * a static screen that is blocked in a key-wait and produces no game frame. */
void gfx_repaint(void);

/* Title-screen hi-res. Asks SDL change resolution to 640x350 and returns whether that took. */
bool video_setHiRes(void);

/* Hi-res (640x350) title surface and its present. picBlit decodes the planar
 * Title640.pic into this surface; gfx_presentHiRes pushes it to the renderer. */
struct SDL_Surface *gfx_getHiResSurface(void);
void gfx_presentHiRes(void);

/* ---- Off-buffer save/restore images ----
 * A save-under is an owned r2d image: capture a page region into it, draw it back
 * later. These thin wrappers bridge page indices to the r2d image API so the game
 * code stays free of r2d/SDL surface details. Coordinates match gfx_copyRect's,
 * so a save-under maps 1:1 onto the same (x,y) in the image. */
struct R2DImage;
struct R2DImage *gfx_allocImage(int w, int h);  /* blank owned image; NULL on failure */
void gfx_freeImage(struct R2DImage *img);        /* release; safe on NULL */
/* Copy a w x h rect from page `srcPage` (srcX,srcY) into `img` at (dstX,dstY). */
void gfx_captureToImage(struct R2DImage *img, int srcPage, int srcX, int srcY,
                        int dstX, int dstY, int w, int h);
/* Copy a w x h rect from `img` (srcX,srcY) into page `dstPage` at (dstX,dstY). */
void gfx_restoreFromImage(struct R2DImage *img, int dstPage, int srcX, int srcY,
                          int dstX, int dstY, int w, int h);
/* Opaque copy of a w x h rect from sprite buffer `handle` (srcX,srcY) into page
 * `dstPage` at (dstX,dstY). For asset sheets drawn opaquely (e.g. the debrief
 * popup icons), as distinct from gfx_blitSprite's transparent (skip-index-0) blit. */
void gfx_drawSpriteOpaque(int handle, int srcX, int srcY, int dstPage,
                          int dstX, int dstY, int w, int h);

/* ---- graphics slots (the public draw API, first slot 0, 84 used) ---- */
/* dseg:0xab8 */
int gfx_allocSpriteBuf(void);                                                 /* alloc a sprite-sheet surface, returns a handle */
void gfx_freeSpriteBuf(int handle);                                           /* release a sprite-sheet surface handle */
void FAR CDECL gfx_fillDirty(int16 *params, const char *string);              /* slot 0x01: clipped glyph variant (vertical window) */
void FAR CDECL gfx_blitTransparent(int16 *params, const char *string);        /* slot 0x02: clipped glyph variant (horizontal window) */
void FAR CDECL gfx_blitVariant(int16 *params, const char *string);            /* slot 0x03: clipped glyph variant (horizontal window) */
void FAR CDECL gfx_copyBlock(int16 *params, const char *string);              /* slot 0x04: glyph blit core (no clip) */
void FAR CDECL gfx_drawString(int16 *pageNum, const char *string);            /* slot 0x05: draw clipped string */
void FAR CDECL gfx_drawStringUnclipped(int16 *params, const char *string);    /* slot 0x06: draw string (both clip windows) */
void FAR CDECL gfx_complexRender(int bxArg, int dxArg, int cxArg, int siArg); /* slot 0x0b: HUD pitch-ladder renderer */
/* Writable pixels + stride of a page (its surface is the single back buffer), for
 * the egame HUD primitives that fill the page directly (eghudr fillSpanRect). */
uint8 *gfx_pagePixels(int page, int *pitchOut);
int FAR CDECL gfx_blitSprite(struct SpriteParams *spritePtr);                                                                     /* slot 0x11: sprite blit */
void FAR CDECL gfx_setBlitOffset2();                                                                                              /* slot 0x18: setBlitOffset */
void FAR CDECL gfx_setBlitOffset(int offset);                                                                                     /* slot 0x1a: setBlitOffset */
int FAR CDECL gfx_getPresetOffset1();                                                                                             /* slot 0x1c: returns baked constant 0x5580 */
int FAR CDECL gfx_getPresetOffset2();                                                                                             /* slot 0x1d: returns baked constant 0x1950 */
int FAR CDECL gfx_getBlitOffset();                                                                                                /* slot 0x1e: returns live blitOffset (cs:0x1a0) */
void FAR CDECL gfx_drawLine(uint16 x1, uint16 y1, uint16 x2, uint16 y2);                                                          /* slot 0x1f: drawLine (Bresenham) */
void FAR CDECL gfx_setDrawColor(uint16 color);                                                                                    /* slot 0x20: set fill/draw colour */
void FAR CDECL gfx_setColor(int color);                                                                                           /* slot 0x21: set fill/draw color */
void FAR CDECL gfx_nop22();                                                                                                       /* slot 0x22: bare RETF no-op (does NOT reset blitOffset) */
void FAR CDECL gfx_nop23();                                                                                                       /* slot 0x23: bare RETF no-op */
void FAR CDECL gfx_dirtyRect(int16 *spanBuf, int yMin, int yMax);                                                                 /* slot 0x25: dirtyRect (reg-called: BX=spanBuf AX=yMin CX=yMax) */
void FAR CDECL gfx_dirtyRect2(const int16 *spanMinBuf, uint16 yMin, uint16 yMax);                                                 /* slot 0x25/0x28: fill dirty spans */
void FAR CDECL gfx_switchColor(int16 *pageDesc, int x1, int y1, int x2, int y2, int oldColor, int newColor);                      /* slot 0x29: replace color in rect */
void FAR CDECL gfx_copyRect(int srcPage, uint16 srcX, uint16 srcY, int dstPage, uint16 dstX, uint16 dstY, int width, int height); /* slot 0x2a: copyRect between pages */
void FAR CDECL gfx_dacAnimate();                                                                                                  /* slot 0x2c: DAC palette animation */
void FAR CDECL gfx_dacCycle();                                                                                                    /* slot 0x2e: DAC fire/colour-cycle animation */
int FAR CDECL gfx_setFont(uint16 ch, uint16 fontIdx);                                                                             /* slot 0x2f: setup font metrics */
int FAR CDECL gfx_getAuxBufSize();                                                                                                /* slot 0x31: getAuxBufSize */
int FAR CDECL gfx_getRowOffset(int y);                                                                                            /* slot 0x3a: returns y*320 */
/* dseg:0xbe4 */
void FAR CDECL gfx_setMode13(void);          /* slot 0x3c: switch to 320x200 (lo-res) */
void FAR CDECL gfx_setFadeSteps(int steps);  /* slot 0x3d: setFadeSteps */
int FAR CDECL gfx_calcRowAddr(int y, int x); /* slot 0x3e: calcRowAddr */
/* dseg:0xbf3 */
int FAR CDECL gfx_getModecode();                                              /* slot 0x3f: returns 3 (MCGA) */
void FAR CDECL gfx_setOvlVal1(int val);                                       /* slot 0x40: writes ds:0xcc */
void FAR CDECL gfx_setOvlVal2(int val);                                       /* slot 0x41: writes ds:0xce */
void FAR CDECL gfx_setDac(uint16 palIdx);                                     /* slot 0x44: set VGA DAC palette */
void gfx_setDacRange(uint16 startReg, uint16 count, const uint8 *vgaTriples); /* native INT 10h AX=1012h: load DAC register block */
void FAR CDECL gfx_waitRetrace();                                             /* slot 0x45: wait for vblank */
void FAR CDECL gfx_flipPage();                                                /* slot 0x46: vblank + flip to VGA */
void FAR CDECL gfx_blitSpriteClipped(int16 *ptr);                             /* slot 0x47: sprite variant */
void FAR CDECL gfx_blitSpriteOpaque(int16 *ptr);                              /* slot 0x49: sprite blit (=0x11) */
/* dseg:0xc2f */
int FAR CDECL gfx_getModeFlag();                         /* slot 0x4c: getModeFlag */
void FAR CDECL gfx_setDacAnimCount(uint16 count);        /* slot 0x4f: setDacAnimCount */
void FAR CDECL gfx_commitPage();                         /* slot 0x50: commitPage */

#endif /* F15_SE2_GFX */
