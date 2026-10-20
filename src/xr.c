/* OpenXR VR mode — see xr.h.
 *
 * Linux/X11 only: SteamVR accepts GL through XR_KHR_opengl_enable with an
 * Xlib/GLX context, so the VR build runs SDL on X11 (gfx_videoInit). The session
 * binds to the GL backend's own context. Every OpenXR call that may touch GL runs on
 * the main thread with that context current (the extension requires it). */
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <X11/Xlib.h>
#include <GL/glx.h>

#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "xr.h"
#include "log.h"

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

/* Default virtual screen placement in the seated LOCAL space (metres; -Z forward),
 * used whenever the GL backend hasn't placed it for a flight frame (xr_setScreen). */
static const float SCREEN_DISTANCE = 1.5f;
static const float SCREEN_WIDTH = 1.6f;

/* The runtime treats an app that misses frames as hung (it keeps its own menu up,
 * then kills it), but the game presents only on change in menus and blocks in wait
 * loops. Past this gap since the last frame, xr_idle re-submits the last image. */
static const Uint64 XR_KEEPALIVE_NS = 5 * SDL_NS_PER_MS;

#define XR_MAX_EYES 2

typedef struct {
    XrSwapchain handle;
    int w, h;
    XrSwapchainImageOpenGLKHR *images;
    uint32_t nImages;
} Chain;

static XrInstance s_instance = XR_NULL_HANDLE;
static XrSystemId s_system = XR_NULL_SYSTEM_ID;
static XrSession s_session = XR_NULL_HANDLE;
static XrSpace s_baseSpace = XR_NULL_HANDLE; /* LOCAL as the runtime defines it */
static XrSpace s_space = XR_NULL_HANDLE;     /* LOCAL re-anchored at the recentered head */
static XrSpace s_viewSpace = XR_NULL_HANDLE; /* VIEW: the head */
static XrSessionState s_state = XR_SESSION_STATE_UNKNOWN;
static int s_running; /* between xrBeginSession and xrEndSession */
static Uint64 s_lastFrameNs;
static int64_t s_swapFormat;

/* The flat screen (whole window, or the cockpit with a see-through windscreen). */
static Chain s_screen;
static int s_haveImage; /* a released screen image holds a copied window frame */
static int s_screenSet; /* xr_setScreen placed it for the open frame */
static float s_scrX, s_scrY, s_scrDist, s_scrW, s_scrH;

/* Stereo eyes: a swapchain each, plus our own depth/stencil renderbuffer. */
static int s_nEyes;
static Chain s_eyes[XR_MAX_EYES];
static GLuint s_eyeDepth[XR_MAX_EYES];
static XrView s_views[XR_MAX_EYES];
static int s_eyesRendered;

/* An open frame: xr_beginFrame ran xrWaitFrame/xrBeginFrame; the next present ends it. */
static int s_frameOpen;
static XrFrameState s_fs;

static float s_headRot[9]; /* head orientation in s_space, row-major, this frame */
static int s_recenterPending;
static int s_prevStereo; /* the last submitted frame carried the stereo world */

static GLuint s_fbo;
static PFNGLGENFRAMEBUFFERSPROC p_glGenFramebuffers;
static PFNGLDELETEFRAMEBUFFERSPROC p_glDeleteFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC p_glBindFramebuffer;
static PFNGLFRAMEBUFFERTEXTURE2DPROC p_glFramebufferTexture2D;
static PFNGLBLITFRAMEBUFFERPROC p_glBlitFramebuffer;
static PFNGLGENRENDERBUFFERSPROC p_glGenRenderbuffers;
static PFNGLDELETERENDERBUFFERSPROC p_glDeleteRenderbuffers;
static PFNGLBINDRENDERBUFFERPROC p_glBindRenderbuffer;
static PFNGLRENDERBUFFERSTORAGEPROC p_glRenderbufferStorage;
static PFNGLFRAMEBUFFERRENDERBUFFERPROC p_glFramebufferRenderbuffer;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC p_glCheckFramebufferStatus;

static int xrOk(XrResult r, const char *what) {
    if (XR_SUCCEEDED(r)) return 1;
    {
        char msg[XR_MAX_RESULT_STRING_SIZE];
        msg[0] = 0;
        if (s_instance) xrResultToString(s_instance, r, msg);
        LogWarn(("OpenXR: %s failed: %s (%d)", what, msg, (int)r));
    }
    return 0;
}

static int hasExtension(const char *name) {
    uint32_t n = 0, i;
    XrExtensionProperties *props;
    int found = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL)) || n == 0) return 0;
    props = (XrExtensionProperties *)SDL_calloc(n, sizeof *props);
    if (!props) return 0;
    for (i = 0; i < n; i++) props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(NULL, n, &n, props)))
        for (i = 0; i < n && !found; i++)
            found = SDL_strcmp(props[i].extensionName, name) == 0;
    SDL_free(props);
    return found;
}

int xr_probe(void) {
    XrInstanceCreateInfo ci;
    XrSystemGetInfo sgi;
    XrInstanceProperties ip;
    XrSystemProperties sp;
    const char *exts[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };

    if (!hasExtension(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) {
        LogInfo(("OpenXR: no runtime with %s; VR off", XR_KHR_OPENGL_ENABLE_EXTENSION_NAME));
        return 0;
    }

    SDL_zero(ci);
    ci.type = XR_TYPE_INSTANCE_CREATE_INFO;
    SDL_strlcpy(ci.applicationInfo.applicationName, "F-15 Strike Eagle II EX",
                XR_MAX_APPLICATION_NAME_SIZE);
    ci.applicationInfo.applicationVersion = 1;
    SDL_strlcpy(ci.applicationInfo.engineName, "f15se2-ex", XR_MAX_ENGINE_NAME_SIZE);
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = exts;
    if (!xrOk(xrCreateInstance(&ci, &s_instance), "xrCreateInstance")) {
        s_instance = XR_NULL_HANDLE;
        return 0;
    }

    SDL_zero(ip);
    ip.type = XR_TYPE_INSTANCE_PROPERTIES;
    if (XR_SUCCEEDED(xrGetInstanceProperties(s_instance, &ip)))
        LogInfo(("OpenXR runtime: %s %u.%u.%u", ip.runtimeName,
                 (unsigned)XR_VERSION_MAJOR(ip.runtimeVersion),
                 (unsigned)XR_VERSION_MINOR(ip.runtimeVersion),
                 (unsigned)XR_VERSION_PATCH(ip.runtimeVersion)));

    SDL_zero(sgi);
    sgi.type = XR_TYPE_SYSTEM_GET_INFO;
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!xrOk(xrGetSystem(s_instance, &sgi, &s_system), "xrGetSystem (headset connected?)")) {
        xr_shutdown();
        return 0;
    }
    SDL_zero(sp);
    sp.type = XR_TYPE_SYSTEM_PROPERTIES;
    if (XR_SUCCEEDED(xrGetSystemProperties(s_instance, s_system, &sp)))
        LogInfo(("OpenXR system: %s", sp.systemName));
    return 1;
}

#define LOAD_GL(var, type, name) (var = (type)SDL_GL_GetProcAddress(name)) != NULL
static int loadGLProcs(void) {
    return LOAD_GL(p_glGenFramebuffers, PFNGLGENFRAMEBUFFERSPROC, "glGenFramebuffers") &&
           LOAD_GL(p_glDeleteFramebuffers, PFNGLDELETEFRAMEBUFFERSPROC, "glDeleteFramebuffers") &&
           LOAD_GL(p_glBindFramebuffer, PFNGLBINDFRAMEBUFFERPROC, "glBindFramebuffer") &&
           LOAD_GL(p_glFramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC, "glFramebufferTexture2D") &&
           LOAD_GL(p_glBlitFramebuffer, PFNGLBLITFRAMEBUFFERPROC, "glBlitFramebuffer") &&
           LOAD_GL(p_glGenRenderbuffers, PFNGLGENRENDERBUFFERSPROC, "glGenRenderbuffers") &&
           LOAD_GL(p_glDeleteRenderbuffers, PFNGLDELETERENDERBUFFERSPROC, "glDeleteRenderbuffers") &&
           LOAD_GL(p_glBindRenderbuffer, PFNGLBINDRENDERBUFFERPROC, "glBindRenderbuffer") &&
           LOAD_GL(p_glRenderbufferStorage, PFNGLRENDERBUFFERSTORAGEPROC, "glRenderbufferStorage") &&
           LOAD_GL(p_glFramebufferRenderbuffer, PFNGLFRAMEBUFFERRENDERBUFFERPROC,
                   "glFramebufferRenderbuffer") &&
           LOAD_GL(p_glCheckFramebufferStatus, PFNGLCHECKFRAMEBUFFERSTATUSPROC,
                   "glCheckFramebufferStatus");
}

/* Fill the Xlib/GLX binding from the context SDL made current. */
static int fillGlxBinding(XrGraphicsBindingOpenGLXlibKHR *b) {
    Display *dpy = glXGetCurrentDisplay();
    GLXContext ctx = glXGetCurrentContext();
    int fbId = 0, screen = 0, n = 0;
    GLXFBConfig *cfgs;
    if (!dpy || !ctx) {
        LogWarn(("OpenXR: no current GLX context (VR needs SDL_VIDEODRIVER=x11)"));
        return 0;
    }
    SDL_zerop(b);
    b->type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR;
    b->xDisplay = dpy;
    b->glxContext = ctx;
    b->glxDrawable = glXGetCurrentDrawable();
    glXQueryContext(dpy, ctx, GLX_FBCONFIG_ID, &fbId);
    glXQueryContext(dpy, ctx, GLX_SCREEN, &screen);
    {
        const int attrs[] = { GLX_FBCONFIG_ID, fbId, None };
        cfgs = glXChooseFBConfig(dpy, screen, attrs, &n);
    }
    if (cfgs && n > 0) {
        XVisualInfo *vi = glXGetVisualFromFBConfig(dpy, cfgs[0]);
        b->glxFBConfig = cfgs[0];
        if (vi) {
            b->visualid = (uint32_t)vi->visualid;
            XFree(vi);
        }
    }
    if (cfgs) XFree(cfgs);
    return 1;
}

/* Prefer an sRGB swapchain: the palette colours are sRGB-encoded, and we write them
 * with GL_FRAMEBUFFER_SRGB off (stored as-is), which an sRGB image displays
 * correctly. A UNORM image would be read as linear and look washed out. */
static int pickSwapFormat(void) {
    uint32_t n = 0, i;
    int64_t *fmts;
    if (XR_FAILED(xrEnumerateSwapchainFormats(s_session, 0, &n, NULL)) || n == 0) return 0;
    fmts = (int64_t *)SDL_calloc(n, sizeof *fmts);
    if (!fmts) return 0;
    xrEnumerateSwapchainFormats(s_session, n, &n, fmts);
    s_swapFormat = 0;
    for (i = 0; i < n; i++)
        if (fmts[i] == GL_SRGB8_ALPHA8) s_swapFormat = fmts[i];
    for (i = 0; i < n && !s_swapFormat; i++)
        if (fmts[i] == GL_RGBA8) s_swapFormat = fmts[i];
    if (!s_swapFormat) s_swapFormat = fmts[0];
    SDL_free(fmts);
    LogInfo(("OpenXR: swapchain format 0x%x", (unsigned)s_swapFormat));
    return 1;
}

static void destroyChain(Chain *c) {
    if (c->handle) xrDestroySwapchain(c->handle);
    SDL_free(c->images);
    SDL_zerop(c);
}

static int createChain(Chain *c, int w, int h) {
    XrSwapchainCreateInfo sci;
    uint32_t i;
    destroyChain(c);
    SDL_zero(sci);
    sci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    sci.format = s_swapFormat;
    sci.sampleCount = 1;
    sci.width = (uint32_t)w;
    sci.height = (uint32_t)h;
    sci.faceCount = 1;
    sci.arraySize = 1;
    sci.mipCount = 1;
    if (!xrOk(xrCreateSwapchain(s_session, &sci, &c->handle), "xrCreateSwapchain")) {
        c->handle = XR_NULL_HANDLE;
        return 0;
    }
    xrEnumerateSwapchainImages(c->handle, 0, &c->nImages, NULL);
    c->images = (XrSwapchainImageOpenGLKHR *)SDL_calloc(c->nImages, sizeof *c->images);
    if (!c->images) {
        destroyChain(c);
        return 0;
    }
    for (i = 0; i < c->nImages; i++) c->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
    if (!xrOk(xrEnumerateSwapchainImages(c->handle, c->nImages, &c->nImages,
                                         (XrSwapchainImageBaseHeader *)c->images),
              "xrEnumerateSwapchainImages")) {
        destroyChain(c);
        return 0;
    }
    c->w = w;
    c->h = h;
    return 1;
}

/* Acquire + wait the next image of `c`; returns its GL texture (0 on failure). */
static GLuint acquireImage(Chain *c) {
    XrSwapchainImageAcquireInfo ai;
    XrSwapchainImageWaitInfo wi;
    uint32_t idx = 0;
    SDL_zero(ai);
    ai.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    if (!xrOk(xrAcquireSwapchainImage(c->handle, &ai, &idx), "xrAcquireSwapchainImage")) return 0;
    SDL_zero(wi);
    wi.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    wi.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(c->handle, &wi);
    return c->images[idx].image;
}

static void releaseImage(Chain *c) {
    XrSwapchainImageReleaseInfo ri;
    SDL_zero(ri);
    ri.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
    xrReleaseSwapchainImage(c->handle, &ri);
}

/* One swapchain + depth/stencil renderbuffer per eye at the runtime's recommended size. */
static int createEyes(void) {
    XrViewConfigurationView vcv[XR_MAX_EYES];
    uint32_t n = 0, i;
    if (XR_FAILED(xrEnumerateViewConfigurationViews(s_instance, s_system,
                                                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                    0, &n, NULL)) ||
        n == 0)
        return 0;
    if (n > XR_MAX_EYES) n = XR_MAX_EYES;
    for (i = 0; i < n; i++) {
        SDL_zero(vcv[i]);
        vcv[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    }
    if (!xrOk(xrEnumerateViewConfigurationViews(s_instance, s_system,
                                                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, n, &n,
                                                vcv),
              "xrEnumerateViewConfigurationViews"))
        return 0;
    for (i = 0; i < n; i++) {
        int w = (int)vcv[i].recommendedImageRectWidth, h = (int)vcv[i].recommendedImageRectHeight;
        if (!createChain(&s_eyes[i], w, h)) return 0;
        p_glGenRenderbuffers(1, &s_eyeDepth[i]);
        p_glBindRenderbuffer(GL_RENDERBUFFER, s_eyeDepth[i]);
        p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        p_glBindRenderbuffer(GL_RENDERBUFFER, 0);
        LogInfo(("OpenXR: eye %u %dx%d", (unsigned)i, w, h));
    }
    s_nEyes = (int)n;
    return 1;
}

int xr_startSession(void) {
    PFN_xrGetOpenGLGraphicsRequirementsKHR getReqs = NULL;
    XrGraphicsRequirementsOpenGLKHR reqs;
    XrGraphicsBindingOpenGLXlibKHR binding;
    XrSessionCreateInfo sci;
    XrReferenceSpaceCreateInfo rsci;

    if (!s_instance) return 0;
    if (!loadGLProcs()) {
        LogWarn(("OpenXR: GL framebuffer objects unavailable; VR off"));
        xr_shutdown();
        return 0;
    }
    /* Mandatory before xrCreateSession. */
    xrGetInstanceProcAddr(s_instance, "xrGetOpenGLGraphicsRequirementsKHR",
                          (PFN_xrVoidFunction *)&getReqs);
    SDL_zero(reqs);
    reqs.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
    if (!getReqs || !xrOk(getReqs(s_instance, s_system, &reqs),
                          "xrGetOpenGLGraphicsRequirementsKHR") ||
        !fillGlxBinding(&binding)) {
        xr_shutdown();
        return 0;
    }

    SDL_zero(sci);
    sci.type = XR_TYPE_SESSION_CREATE_INFO;
    sci.next = &binding;
    sci.systemId = s_system;
    if (!xrOk(xrCreateSession(s_instance, &sci, &s_session), "xrCreateSession")) {
        s_session = XR_NULL_HANDLE;
        xr_shutdown();
        return 0;
    }

    SDL_zero(rsci);
    rsci.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL; /* seated */
    rsci.poseInReferenceSpace.orientation.w = 1.0f;
    if (!xrOk(xrCreateReferenceSpace(s_session, &rsci, &s_baseSpace), "xrCreateReferenceSpace") ||
        !xrOk(xrCreateReferenceSpace(s_session, &rsci, &s_space), "xrCreateReferenceSpace") ||
        !pickSwapFormat()) {
        xr_shutdown();
        return 0;
    }
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!xrOk(xrCreateReferenceSpace(s_session, &rsci, &s_viewSpace), "xrCreateReferenceSpace")) {
        xr_shutdown();
        return 0;
    }
    p_glGenFramebuffers(1, &s_fbo);
    if (!createEyes()) {
        LogWarn(("OpenXR: no stereo views; 3D stays on the flat screen"));
        s_nEyes = 0;
    }
    LogInfo(("OpenXR: session created"));
    return 1;
}

void xr_shutdown(void) {
    int i;
    for (i = 0; i < XR_MAX_EYES; i++) {
        destroyChain(&s_eyes[i]);
        if (s_eyeDepth[i] && p_glDeleteRenderbuffers) p_glDeleteRenderbuffers(1, &s_eyeDepth[i]);
        s_eyeDepth[i] = 0;
    }
    s_nEyes = 0;
    if (s_fbo && p_glDeleteFramebuffers) p_glDeleteFramebuffers(1, &s_fbo);
    s_fbo = 0;
    destroyChain(&s_screen);
    s_haveImage = 0;
    if (s_viewSpace) xrDestroySpace(s_viewSpace);
    s_viewSpace = XR_NULL_HANDLE;
    if (s_space) xrDestroySpace(s_space);
    s_space = XR_NULL_HANDLE;
    if (s_baseSpace) xrDestroySpace(s_baseSpace);
    s_baseSpace = XR_NULL_HANDLE;
    if (s_session) xrDestroySession(s_session);
    s_session = XR_NULL_HANDLE;
    if (s_instance) xrDestroyInstance(s_instance);
    s_instance = XR_NULL_HANDLE;
    s_system = XR_NULL_SYSTEM_ID;
    s_running = 0;
    s_frameOpen = 0;
    s_state = XR_SESSION_STATE_UNKNOWN;
}

static void pollEvents(void) {
    XrEventDataBuffer ev;
    for (;;) {
        SDL_zero(ev);
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        if (xrPollEvent(s_instance, &ev) != XR_SUCCESS) break;
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrEventDataSessionStateChanged *sc = (const XrEventDataSessionStateChanged *)&ev;
            s_state = sc->state;
            LogInfo(("OpenXR: session state %d", (int)s_state));
            if (s_state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi;
                SDL_zero(bi);
                bi.type = XR_TYPE_SESSION_BEGIN_INFO;
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                s_running = xrOk(xrBeginSession(s_session, &bi), "xrBeginSession");
            } else if (s_state == XR_SESSION_STATE_STOPPING) {
                xrEndSession(s_session);
                s_running = 0;
            } else if (s_state == XR_SESSION_STATE_EXITING ||
                       s_state == XR_SESSION_STATE_LOSS_PENDING) {
                LogInfo(("OpenXR: session ended by the runtime; continuing flat"));
                xr_shutdown();
                return;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            LogInfo(("OpenXR: instance lost; continuing flat"));
            xr_shutdown();
            return;
        }
    }
}

/* xrWaitFrame + xrBeginFrame, leaving the frame open for xr_bindEye / endFrame. */
static int openFrame(void) {
    XrFrameWaitInfo fwi;
    XrFrameBeginInfo fbi;
    SDL_zero(fwi);
    fwi.type = XR_TYPE_FRAME_WAIT_INFO;
    SDL_zero(s_fs);
    s_fs.type = XR_TYPE_FRAME_STATE;
    if (!xrOk(xrWaitFrame(s_session, &fwi, &s_fs), "xrWaitFrame")) return 0;
    SDL_zero(fbi);
    fbi.type = XR_TYPE_FRAME_BEGIN_INFO;
    if (!xrOk(xrBeginFrame(s_session, &fbi), "xrBeginFrame")) return 0;
    s_frameOpen = 1;
    s_eyesRendered = 0;
    return 1;
}

/* Copy the window's (single-sampled) back buffer into the next screen image. */
static int blitWindowToScreen(int w, int h) {
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLuint tex = acquireImage(&s_screen);
    if (!tex) return 0;
    glDisable(GL_SCISSOR_TEST); /* the blit honours the scissor */
    p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_fbo);
    p_glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    p_glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    p_glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    releaseImage(&s_screen);
    return 1;
}

/* Submit the open frame: the stereo projection layer (if the eyes were drawn) under
 * the screen quad. `win` copies the window's back buffer onto the screen; NULL
 * re-shows the last copied image (the game didn't present this frame). */
static void closeFrame(SDL_Window *win) {
    XrFrameEndInfo fei;
    XrCompositionLayerProjection proj;
    XrCompositionLayerProjectionView pv[XR_MAX_EYES];
    XrCompositionLayerQuad quad;
    const XrCompositionLayerBaseHeader *layers[2];
    int nLayers = 0, i, w = 0, h = 0;

    if (win && s_fs.shouldRender) {
        SDL_GetWindowSizeInPixels(win, &w, &h);
        if (w > 0 && h > 0 && (s_screen.handle && s_screen.w == w && s_screen.h == h
                                   ? 1
                                   : createChain(&s_screen, w, h)) &&
            blitWindowToScreen(w, h))
            s_haveImage = 1;
    }

    if (s_fs.shouldRender && s_eyesRendered) {
        SDL_zero(proj);
        proj.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        proj.space = s_space;
        proj.viewCount = (uint32_t)s_nEyes;
        proj.views = pv;
        for (i = 0; i < s_nEyes; i++) {
            SDL_zero(pv[i]);
            pv[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            pv[i].pose = s_views[i].pose;
            pv[i].fov = s_views[i].fov;
            pv[i].subImage.swapchain = s_eyes[i].handle;
            pv[i].subImage.imageRect.extent.width = s_eyes[i].w;
            pv[i].subImage.imageRect.extent.height = s_eyes[i].h;
        }
        layers[nLayers++] = (const XrCompositionLayerBaseHeader *)&proj;
    }
    if (s_fs.shouldRender && s_haveImage) {
        SDL_zero(quad);
        quad.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
        quad.space = s_space;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = s_screen.handle;
        quad.subImage.imageRect.extent.width = s_screen.w;
        quad.subImage.imageRect.extent.height = s_screen.h;
        quad.pose.orientation.w = 1.0f;
        if (s_screenSet && s_eyesRendered) {
            /* Cockpit over the stereo world: its windscreen is transparent. */
            quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            quad.pose.position.x = s_scrX;
            quad.pose.position.y = s_scrY;
            quad.pose.position.z = -s_scrDist;
            quad.size.width = s_scrW;
            quad.size.height = s_scrH;
        } else {
            quad.pose.position.z = -SCREEN_DISTANCE;
            quad.size.width = SCREEN_WIDTH;
            quad.size.height = SCREEN_WIDTH * (float)s_screen.h / (float)s_screen.w;
        }
        layers[nLayers++] = (const XrCompositionLayerBaseHeader *)&quad;
    }

    SDL_zero(fei);
    fei.type = XR_TYPE_FRAME_END_INFO;
    fei.displayTime = s_fs.predictedDisplayTime;
    fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    fei.layerCount = (uint32_t)nLayers;
    fei.layers = layers;
    xrOk(xrEndFrame(s_session, &fei), "xrEndFrame");
    s_frameOpen = 0;
    s_screenSet = 0;
    s_prevStereo = s_eyesRendered;
    s_lastFrameNs = SDL_GetTicksNS();
}

/* Re-anchor the seated space at the head: its current position, facing its current
 * yaw (pitch/roll stay level), so the cockpit sits straight ahead of the pilot. */
static void recenter(XrTime time) {
    XrSpaceLocation loc;
    XrReferenceSpaceCreateInfo rsci;
    XrSpace space;
    float x, y, z, w, fx, fz, yaw;
    SDL_zero(loc);
    loc.type = XR_TYPE_SPACE_LOCATION;
    if (XR_FAILED(xrLocateSpace(s_viewSpace, s_baseSpace, time, &loc)) ||
        !(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) ||
        !(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        return;
    x = loc.pose.orientation.x; y = loc.pose.orientation.y;
    z = loc.pose.orientation.z; w = loc.pose.orientation.w;
    /* forward = R * (0,0,-1) */
    fx = -2.0f * (x * z + y * w);
    fz = -(1.0f - 2.0f * (x * x + y * y));
    yaw = SDL_atan2f(-fx, -fz);
    SDL_zero(rsci);
    rsci.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.position = loc.pose.position;
    rsci.poseInReferenceSpace.orientation.y = SDL_sinf(yaw * 0.5f);
    rsci.poseInReferenceSpace.orientation.w = SDL_cosf(yaw * 0.5f);
    if (!xrOk(xrCreateReferenceSpace(s_session, &rsci, &space), "xrCreateReferenceSpace")) return;
    xrDestroySpace(s_space);
    s_space = space;
    s_recenterPending = 0;
    LogInfo(("OpenXR: recentered"));
}

/* Row-major rotation matrix of a quaternion. */
static void quatToMatrix(const XrQuaternionf *q, float *m) {
    float x = q->x, y = q->y, z = q->z, w = q->w;
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y - z * w);     m[2] = 2 * (x * z + y * w);
    m[3] = 2 * (x * y + z * w);     m[4] = 1 - 2 * (x * x + z * z); m[5] = 2 * (y * z - x * w);
    m[6] = 2 * (x * z - y * w);     m[7] = 2 * (y * z + x * w);     m[8] = 1 - 2 * (x * x + y * y);
}

void xr_recenter(void) { s_recenterPending = 1; }

void xr_headRotation(float *m) { SDL_memcpy(m, s_headRot, sizeof s_headRot); }

int xr_beginFrame(void) {
    XrViewLocateInfo li;
    XrViewState vs;
    uint32_t n = 0;
    int i;
    if (!s_session || s_frameOpen) return 0;
    pollEvents();
    if (!s_session || !s_running || !openFrame()) return 0;
    if (!s_fs.shouldRender || s_nEyes == 0) return 0;
    if (!s_prevStereo) s_recenterPending = 1; /* entering flight */
    if (s_recenterPending) recenter(s_fs.predictedDisplayTime);
    {
        XrSpaceLocation loc;
        SDL_zero(loc);
        loc.type = XR_TYPE_SPACE_LOCATION;
        if (XR_FAILED(xrLocateSpace(s_viewSpace, s_space, s_fs.predictedDisplayTime, &loc)) ||
            !(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
            return 0;
        quatToMatrix(&loc.pose.orientation, s_headRot);
    }

    SDL_zero(li);
    li.type = XR_TYPE_VIEW_LOCATE_INFO;
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = s_fs.predictedDisplayTime;
    li.space = s_space;
    SDL_zero(vs);
    vs.type = XR_TYPE_VIEW_STATE;
    for (i = 0; i < XR_MAX_EYES; i++) {
        SDL_zero(s_views[i]);
        s_views[i].type = XR_TYPE_VIEW;
    }
    if (!xrOk(xrLocateViews(s_session, &li, &vs, XR_MAX_EYES, &n, s_views), "xrLocateViews") ||
        (int)n != s_nEyes ||
        !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
        return 0;
    return s_nEyes;
}

/* Column-major GL projection for an asymmetric OpenXR FOV. */
static void fovProjection(const XrFovf *fov, float n, float f, float *m) {
    float l = SDL_tanf(fov->angleLeft), r = SDL_tanf(fov->angleRight);
    float d = SDL_tanf(fov->angleDown), u = SDL_tanf(fov->angleUp);
    SDL_memset(m, 0, 16 * sizeof *m);
    m[0] = 2.0f / (r - l);
    m[5] = 2.0f / (u - d);
    m[8] = (r + l) / (r - l);
    m[9] = (u + d) / (u - d);
    m[10] = -(f + n) / (f - n);
    m[11] = -1.0f;
    m[14] = -2.0f * f * n / (f - n);
}

/* Column-major view matrix = inverse of the eye pose (rotation^T, -rotation^T * p). */
static void poseView(const XrPosef *p, float *m) {
    float x = p->orientation.x, y = p->orientation.y, z = p->orientation.z, w = p->orientation.w;
    /* rotation rows of R (pose -> LOCAL); the view uses R^T */
    float r00 = 1 - 2 * (y * y + z * z), r01 = 2 * (x * y - z * w), r02 = 2 * (x * z + y * w);
    float r10 = 2 * (x * y + z * w), r11 = 1 - 2 * (x * x + z * z), r12 = 2 * (y * z - x * w);
    float r20 = 2 * (x * z - y * w), r21 = 2 * (y * z + x * w), r22 = 1 - 2 * (x * x + y * y);
    float px = p->position.x, py = p->position.y, pz = p->position.z;
    m[0] = r00; m[4] = r10; m[8] = r20;  m[12] = -(r00 * px + r10 * py + r20 * pz);
    m[1] = r01; m[5] = r11; m[9] = r21;  m[13] = -(r01 * px + r11 * py + r21 * pz);
    m[2] = r02; m[6] = r12; m[10] = r22; m[14] = -(r02 * px + r12 * py + r22 * pz);
    m[3] = 0;   m[7] = 0;   m[11] = 0;   m[15] = 1;
}

int xr_bindEye(int eye, float nearM, float farM, float *proj, float *view, int *w, int *h) {
    GLuint tex;
    if (!s_frameOpen || eye < 0 || eye >= s_nEyes) return 0;
    tex = acquireImage(&s_eyes[eye]);
    if (!tex) return 0;
    p_glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                s_eyeDepth[eye]);
    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LogWarn(("OpenXR: eye framebuffer incomplete"));
        p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        releaseImage(&s_eyes[eye]);
        return 0;
    }
    fovProjection(&s_views[eye].fov, nearM, farM, proj);
    poseView(&s_views[eye].pose, view);
    *w = s_eyes[eye].w;
    *h = s_eyes[eye].h;
    return 1;
}

void xr_releaseEye(int eye) {
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    releaseImage(&s_eyes[eye]);
    if (eye == s_nEyes - 1) s_eyesRendered = 1;
}

void xr_setScreen(float x, float y, float dist, float w, float h) {
    s_screenSet = 1;
    s_scrX = x;
    s_scrY = y;
    s_scrDist = dist;
    s_scrW = w;
    s_scrH = h;
}

int xr_presentWindow(SDL_Window *win) {
    if (!s_session) return 0;
    if (s_frameOpen) {
        closeFrame(win);
        return 1;
    }
    pollEvents();
    if (!s_session) return 0;
    if (!s_running) {
        /* No frame loop to pace us until the runtime starts the session. */
        SDL_Delay(11);
        return 1;
    }
    if (openFrame()) closeFrame(win);
    return 1;
}

void xr_idle(void) {
    if (!s_session || s_frameOpen || SDL_GetTicksNS() - s_lastFrameNs < XR_KEEPALIVE_NS) return;
    pollEvents();
    if (!s_session) return;
    if (s_running) {
        if (openFrame()) closeFrame(NULL);
    } else {
        s_lastFrameNs = SDL_GetTicksNS(); /* poll events at the same cadence */
    }
}
