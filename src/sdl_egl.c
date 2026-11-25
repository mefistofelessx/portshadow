/* sdl_egl.c -- SDL2 + Mali libEGL.so.1 / libGLESv2.so.2 + softfp GLES thunks for M2. */
#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdl_egl.h"

#ifdef __arm__
#define SOFTFP __attribute__((pcs("aapcs"), noinline))
#else
#define SOFTFP
#endif

/* Minimal SDL2 constants so we don't need SDL2 headers at cross-compile time */
#define SDL_INIT_AUDIO          0x00000010U
#define SDL_INIT_VIDEO          0x00000020U
#define SDL_INIT_GAMECONTROLLER 0x00002000U
#define SDL_INIT_EVENTS         0x00004000U

#define SDL_WINDOWPOS_UNDEFINED 0x1FFF0000U
#define SDL_WINDOW_FULLSCREEN   0x00000001U
#define SDL_WINDOW_OPENGL       0x00000002U
#define SDL_WINDOW_SHOWN        0x00000004U
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000U)

#define SDL_GL_RED_SIZE                   0
#define SDL_GL_GREEN_SIZE                 1
#define SDL_GL_BLUE_SIZE                  2
#define SDL_GL_ALPHA_SIZE                 3
#define SDL_GL_DOUBLEBUFFER               5
#define SDL_GL_DEPTH_SIZE                 6
#define SDL_GL_STENCIL_SIZE               7
#define SDL_GL_CONTEXT_MAJOR_VERSION      17
#define SDL_GL_CONTEXT_MINOR_VERSION      18
#define SDL_GL_CONTEXT_PROFILE_MASK       21
#define SDL_GL_CONTEXT_PROFILE_ES         0x0004

#define SDL_QUIT           0x100
#define SDL_KEYDOWN        0x300
#define SDLK_ESCAPE        27

#define EGL_SUCCESS        0x3000
#define EGL_TRUE           1
#define EGL_FALSE          0
#define EGL_DEFAULT_DISPLAY ((void *)0)
#define EGL_NO_CONTEXT     ((void *)0)
#define EGL_NO_SURFACE     ((void *)0)
#define EGL_DRAW           0x3059
#define EGL_WIDTH          0x3057
#define EGL_HEIGHT         0x3056

typedef struct {
    uint32_t format;
    int      w;
    int      h;
    int      refresh_rate;
    void    *driverdata;
} sdl_display_mode_t;

typedef struct {
    uint32_t type;
    uint32_t timestamp;
    uint32_t windowID;
    uint8_t  state;
    uint8_t  repeat;
    uint8_t  padding2;
    uint8_t  padding3;
    int32_t  scancode;
    int32_t  sym;
    uint16_t mod;
    uint32_t unused;
} sdl_key_event_t;

typedef union {
    uint32_t        type;
    sdl_key_event_t key;
    uint8_t         pad[64];
} sdl_event_t;

/* Dynamically loaded SDL2 symbols */
static void *g_libsdl2 = NULL;
static void *g_libegl  = NULL;
static void *g_libgles = NULL;

static int   (*pSDL_Init)(uint32_t flags);
static void  (*pSDL_Quit)(void);
static const char *(*pSDL_GetError)(void);
static int   (*pSDL_GetCurrentDisplayMode)(int displayIndex, sdl_display_mode_t *mode);
static void *(*pSDL_CreateWindow)(const char *title, int x, int y, int w, int h, uint32_t flags);
static void  (*pSDL_DestroyWindow)(void *win);
static void  (*pSDL_GetWindowSize)(void *win, int *w, int *h);
static int   (*pSDL_GL_SetAttribute)(int attr, int value);
static void *(*pSDL_GL_CreateContext)(void *win);
static void  (*pSDL_GL_DeleteContext)(void *ctx);
static int   (*pSDL_GL_MakeCurrent)(void *win, void *ctx);
static int   (*pSDL_GL_SetSwapInterval)(int interval);
static void  (*pSDL_GL_SwapWindow)(void *win);
static int   (*pSDL_PollEvent)(sdl_event_t *ev);

/* Dynamically loaded EGL symbols */
static void        *(*peglGetDisplay)(void *dpy);
static unsigned int (*peglInitialize)(void *dpy, int *maj, int *min);
static unsigned int (*peglTerminate)(void *dpy);
static int          (*peglGetError)(void);
static const char  *(*peglQueryString)(void *dpy, int name);
static unsigned int (*peglChooseConfig)(void *dpy, const int *attr, void **cfgs, int sz, int *num);
static unsigned int (*peglGetConfigAttrib)(void *dpy, void *cfg, int attr, int *val);
static void        *(*peglCreatePbufferSurface)(void *dpy, void *cfg, const int *attr);
static unsigned int (*peglDestroySurface)(void *dpy, void *surf);
static void        *(*peglCreateContext)(void *dpy, void *cfg, void *share, const int *attr);
static unsigned int (*peglDestroyContext)(void *dpy, void *ctx);
static unsigned int (*peglMakeCurrent)(void *dpy, void *draw, void *read, void *ctx);
static void        *(*peglGetCurrentDisplay)(void);
static void        *(*peglGetCurrentContext)(void);
static void        *(*peglGetCurrentSurface)(int readdraw);
static unsigned int (*peglQuerySurface)(void *dpy, void *surf, int attr, int *val);
static unsigned int (*peglSurfaceAttrib)(void *dpy, void *surf, int attr, int val);
static void        *(*peglGetProcAddress)(const char *name);

/* Active SDL2 / EGL state */
static void *g_sdl_win     = NULL;
static void *g_sdl_glctx   = NULL;
static void *g_egl_dpy     = NULL;
static void *g_egl_surf    = NULL;
static void *g_egl_ctx     = NULL;
static void *g_egl_cfg     = NULL;
static int   g_ctx_claimed = 0;
static int   g_win_w       = 720;
static int   g_win_h       = 720;

typedef struct {
    int width;
    int height;
    int format;
} fake_native_window_t;

static fake_native_window_t g_native_win = { 720, 720, 1 };

void *sdl_egl_get_native_window(void) {
    return &g_native_win;
}

void sdl_egl_get_size(int *out_w, int *out_h) {
    if (out_w) *out_w = g_win_w;
    if (out_h) *out_h = g_win_h;
}

int sdl_egl_init(void) {
    g_libsdl2 = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    g_libegl  = dlopen("libEGL.so.1",      RTLD_NOW | RTLD_GLOBAL);
    g_libgles = dlopen("libGLESv2.so.2",   RTLD_NOW | RTLD_GLOBAL);
    if (!g_libsdl2 || !g_libegl || !g_libgles) {
        fprintf(stderr, "[SDL_EGL] dlopen falhou: sdl2=%p egl=%p gles=%p (%s)\n",
                g_libsdl2, g_libegl, g_libgles, dlerror());
        return -1;
    }

#define LOAD_SYM(lib, name) do { \
    p##name = dlsym((lib), #name); \
    if (!p##name) fprintf(stderr, "[SDL_EGL] aviso: simbolo ausente %s\n", #name); \
} while (0)

    LOAD_SYM(g_libsdl2, SDL_Init);
    LOAD_SYM(g_libsdl2, SDL_Quit);
    LOAD_SYM(g_libsdl2, SDL_GetError);
    LOAD_SYM(g_libsdl2, SDL_GetCurrentDisplayMode);
    LOAD_SYM(g_libsdl2, SDL_CreateWindow);
    LOAD_SYM(g_libsdl2, SDL_DestroyWindow);
    LOAD_SYM(g_libsdl2, SDL_GetWindowSize);
    LOAD_SYM(g_libsdl2, SDL_GL_SetAttribute);
    LOAD_SYM(g_libsdl2, SDL_GL_CreateContext);
    LOAD_SYM(g_libsdl2, SDL_GL_DeleteContext);
    LOAD_SYM(g_libsdl2, SDL_GL_MakeCurrent);
    LOAD_SYM(g_libsdl2, SDL_GL_SetSwapInterval);
    LOAD_SYM(g_libsdl2, SDL_GL_SwapWindow);
    LOAD_SYM(g_libsdl2, SDL_PollEvent);

    LOAD_SYM(g_libegl, eglGetDisplay);
    LOAD_SYM(g_libegl, eglInitialize);
    LOAD_SYM(g_libegl, eglTerminate);
    LOAD_SYM(g_libegl, eglGetError);
    LOAD_SYM(g_libegl, eglQueryString);
    LOAD_SYM(g_libegl, eglChooseConfig);
    LOAD_SYM(g_libegl, eglGetConfigAttrib);
    LOAD_SYM(g_libegl, eglCreatePbufferSurface);
    LOAD_SYM(g_libegl, eglDestroySurface);
    LOAD_SYM(g_libegl, eglCreateContext);
    LOAD_SYM(g_libegl, eglDestroyContext);
    LOAD_SYM(g_libegl, eglMakeCurrent);
    LOAD_SYM(g_libegl, eglGetCurrentDisplay);
    LOAD_SYM(g_libegl, eglGetCurrentContext);
    LOAD_SYM(g_libegl, eglGetCurrentSurface);
    LOAD_SYM(g_libegl, eglQuerySurface);
    LOAD_SYM(g_libegl, eglSurfaceAttrib);
    LOAD_SYM(g_libegl, eglGetProcAddress);
#undef LOAD_SYM

    if (!pSDL_Init || !pSDL_CreateWindow || !pSDL_GL_CreateContext) {
        fprintf(stderr, "[SDL_EGL] funcoes essenciais do SDL2 faltando\n");
        return -1;
    }

    if (pSDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "[SDL_EGL] SDL_Init falhou: %s\n", pSDL_GetError ? pSDL_GetError() : "?");
        return -1;
    }

    sdl_display_mode_t mode;
    if (pSDL_GetCurrentDisplayMode && pSDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0) {
        g_win_w = mode.w;
        g_win_h = mode.h;
    }
    printf("[SDL_EGL] Display detectado: %dx%d\n", g_win_w, g_win_h);

    if (pSDL_GL_SetAttribute) {
        pSDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        pSDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        pSDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        pSDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        pSDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        pSDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        pSDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
        pSDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        pSDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        pSDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    }

    g_sdl_win = pSDL_CreateWindow("Shadow Fight 2",
                                  SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  g_win_w, g_win_h,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
    if (!g_sdl_win) {
        g_sdl_win = pSDL_CreateWindow("Shadow Fight 2",
                                      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                      g_win_w, g_win_h,
                                      SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    }
    if (!g_sdl_win) {
        fprintf(stderr, "[SDL_EGL] SDL_CreateWindow falhou: %s\n", pSDL_GetError ? pSDL_GetError() : "?");
        return -1;
    }

    if (pSDL_GetWindowSize)
        pSDL_GetWindowSize(g_sdl_win, &g_win_w, &g_win_h);
    g_native_win.width  = g_win_w;
    g_native_win.height = g_win_h;

    g_sdl_glctx = pSDL_GL_CreateContext(g_sdl_win);
    if (!g_sdl_glctx) {
        fprintf(stderr, "[SDL_EGL] SDL_GL_CreateContext falhou: %s\n", pSDL_GetError ? pSDL_GetError() : "?");
        return -1;
    }
    if (pSDL_GL_MakeCurrent) pSDL_GL_MakeCurrent(g_sdl_win, g_sdl_glctx);
    if (pSDL_GL_SetSwapInterval) pSDL_GL_SetSwapInterval(1);

    g_egl_dpy  = peglGetCurrentDisplay ? peglGetCurrentDisplay() : NULL;
    if (!g_egl_dpy && peglGetDisplay) g_egl_dpy = peglGetDisplay(EGL_DEFAULT_DISPLAY);
    g_egl_surf = peglGetCurrentSurface ? peglGetCurrentSurface(EGL_DRAW) : (void *)0x1;
    g_egl_ctx  = peglGetCurrentContext ? peglGetCurrentContext() : (void *)0x1;

    if (peglChooseConfig && g_egl_dpy) {
        static const int min_attr[] = { 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3040, 4, 0x3038 };
        int n = 0;
        peglChooseConfig(g_egl_dpy, min_attr, &g_egl_cfg, 1, &n);
    }

    const unsigned char *(*pglGetString)(unsigned int) = dlsym(g_libgles, "glGetString");
    if (pglGetString) {
        printf("[SDL_EGL] GL_VENDOR   = %s\n", (const char *)pglGetString(0x1F00));
        printf("[SDL_EGL] GL_RENDERER = %s\n", (const char *)pglGetString(0x1F01));
        printf("[SDL_EGL] GL_VERSION  = %s\n", (const char *)pglGetString(0x1F02));
    }
    printf("[SDL_EGL] Window=%p %dx%d dpy=%p surf=%p ctx=%p cfg=%p\n",
           g_sdl_win, g_win_w, g_win_h, g_egl_dpy, g_egl_surf, g_egl_ctx, g_egl_cfg);
    return 0;
}

void sdl_egl_shutdown(void) {
    if (g_sdl_glctx && pSDL_GL_DeleteContext) { pSDL_GL_DeleteContext(g_sdl_glctx); g_sdl_glctx = NULL; }
    if (g_sdl_win && pSDL_DestroyWindow) { pSDL_DestroyWindow(g_sdl_win); g_sdl_win = NULL; }
    if (pSDL_Quit) pSDL_Quit();
}

int sdl_egl_poll_events(void) {
    if (!pSDL_PollEvent) return 0;
    sdl_event_t ev;
    while (pSDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return 1;
        if (ev.type == SDL_KEYDOWN && ev.key.sym == SDLK_ESCAPE) return 1;
    }
    return 0;
}

/* ── GLESv2 softfp -> hardfp thunks (resolved via eglGetProcAddress) ───── */

static void *resolve_gles(const char *name) {
    void *p = peglGetProcAddress ? peglGetProcAddress(name) : NULL;
    if (!p && g_libgles) p = dlsym(g_libgles, name);
    return p;
}

static SOFTFP void th_glClearColor(float r, float g, float b, float a) {
    static void (*fn)(float, float, float, float);
    if (!fn) fn = resolve_gles("glClearColor");
    if (fn) fn(r, g, b, a);
}
static SOFTFP void th_glClearDepthf(float d) {
    static void (*fn)(float);
    if (!fn) fn = resolve_gles("glClearDepthf");
    if (fn) fn(d);
}
static SOFTFP void th_glDepthRangef(float n, float f) {
    static void (*fn)(float, float);
    if (!fn) fn = resolve_gles("glDepthRangef");
    if (fn) fn(n, f);
}
static SOFTFP void th_glLineWidth(float w) {
    static void (*fn)(float);
    if (!fn) fn = resolve_gles("glLineWidth");
    if (fn) fn(w);
}
static SOFTFP void th_glPolygonOffset(float factor, float units) {
    static void (*fn)(float, float);
    if (!fn) fn = resolve_gles("glPolygonOffset");
    if (fn) fn(factor, units);
}
static SOFTFP void th_glSampleCoverage(float value, unsigned char invert) {
    static void (*fn)(float, unsigned char);
    if (!fn) fn = resolve_gles("glSampleCoverage");
    if (fn) fn(value, invert);
}
static SOFTFP void th_glBlendColor(float r, float g, float b, float a) {
    static void (*fn)(float, float, float, float);
    if (!fn) fn = resolve_gles("glBlendColor");
    if (fn) fn(r, g, b, a);
}
static SOFTFP void th_glTexParameterf(unsigned int target, unsigned int pname, float param) {
    static void (*fn)(unsigned int, unsigned int, float);
    if (!fn) fn = resolve_gles("glTexParameterf");
    if (fn) fn(target, pname, param);
}
static SOFTFP void th_glSamplerParameterf(unsigned int sampler, unsigned int pname, float param) {
    static void (*fn)(unsigned int, unsigned int, float);
    if (!fn) fn = resolve_gles("glSamplerParameterf");
    if (fn) fn(sampler, pname, param);
}
static SOFTFP void th_glUniform1f(int loc, float v0) {
    static void (*fn)(int, float);
    if (!fn) fn = resolve_gles("glUniform1f");
    if (fn) fn(loc, v0);
}
static SOFTFP void th_glUniform2f(int loc, float v0, float v1) {
    static void (*fn)(int, float, float);
    if (!fn) fn = resolve_gles("glUniform2f");
    if (fn) fn(loc, v0, v1);
}
static SOFTFP void th_glUniform3f(int loc, float v0, float v1, float v2) {
    static void (*fn)(int, float, float, float);
    if (!fn) fn = resolve_gles("glUniform3f");
    if (fn) fn(loc, v0, v1, v2);
}
static SOFTFP void th_glUniform4f(int loc, float v0, float v1, float v2, float v3) {
    static void (*fn)(int, float, float, float, float);
    if (!fn) fn = resolve_gles("glUniform4f");
    if (fn) fn(loc, v0, v1, v2, v3);
}
static SOFTFP void th_glVertexAttrib1f(unsigned int idx, float x) {
    static void (*fn)(unsigned int, float);
    if (!fn) fn = resolve_gles("glVertexAttrib1f");
    if (fn) fn(idx, x);
}
static SOFTFP void th_glVertexAttrib2f(unsigned int idx, float x, float y) {
    static void (*fn)(unsigned int, float, float);
    if (!fn) fn = resolve_gles("glVertexAttrib2f");
    if (fn) fn(idx, x, y);
}
static SOFTFP void th_glVertexAttrib3f(unsigned int idx, float x, float y, float z) {
    static void (*fn)(unsigned int, float, float, float);
    if (!fn) fn = resolve_gles("glVertexAttrib3f");
    if (fn) fn(idx, x, y, z);
}
static SOFTFP void th_glVertexAttrib4f(unsigned int idx, float x, float y, float z, float w) {
    static void (*fn)(unsigned int, float, float, float, float);
    if (!fn) fn = resolve_gles("glVertexAttrib4f");
    if (fn) fn(idx, x, y, z, w);
}
static SOFTFP void th_glClearBufferfi(unsigned int buffer, int drawbuffer, float depth, int stencil) {
    static void (*fn)(unsigned int, int, float, int);
    if (!fn) fn = resolve_gles("glClearBufferfi");
    if (fn) fn(buffer, drawbuffer, depth, stencil);
}

typedef struct { const char *name; void *fn; } gles_thunk_t;
static const gles_thunk_t g_gles_thunks[] = {
    { "glClearColor",      (void *)th_glClearColor },
    { "glClearDepthf",     (void *)th_glClearDepthf },
    { "glDepthRangef",     (void *)th_glDepthRangef },
    { "glLineWidth",       (void *)th_glLineWidth },
    { "glPolygonOffset",   (void *)th_glPolygonOffset },
    { "glSampleCoverage",  (void *)th_glSampleCoverage },
    { "glBlendColor",      (void *)th_glBlendColor },
    { "glTexParameterf",   (void *)th_glTexParameterf },
    { "glSamplerParameterf",(void *)th_glSamplerParameterf },
    { "glUniform1f",       (void *)th_glUniform1f },
    { "glUniform2f",       (void *)th_glUniform2f },
    { "glUniform3f",       (void *)th_glUniform3f },
    { "glUniform4f",       (void *)th_glUniform4f },
    { "glVertexAttrib1f",  (void *)th_glVertexAttrib1f },
    { "glVertexAttrib2f",  (void *)th_glVertexAttrib2f },
    { "glVertexAttrib3f",  (void *)th_glVertexAttrib3f },
    { "glVertexAttrib4f",  (void *)th_glVertexAttrib4f },
    { "glClearBufferfi",   (void *)th_glClearBufferfi },
};

/* ── EGL bridge ────────────────────────────────────────────────────────── */

static void *br_eglGetDisplay(void *dpy) {
    (void)dpy;
    return g_egl_dpy;
}

static unsigned int br_eglInitialize(void *dpy, int *maj, int *min) {
    if (peglInitialize && dpy)
        return peglInitialize(dpy, maj, min);
    if (maj) *maj = 1;
    if (min) *min = 4;
    return EGL_TRUE;
}

static unsigned int br_eglTerminate(void *dpy) {
    (void)dpy;
    return EGL_TRUE;
}

static int br_eglGetError(void) {
    return peglGetError ? peglGetError() : EGL_SUCCESS;
}

static const char *br_eglQueryString(void *dpy, int name) {
    return peglQueryString ? peglQueryString(dpy ? dpy : g_egl_dpy, name) : "";
}

static unsigned int br_eglChooseConfig(void *dpy, const int *attr, void **cfgs, int sz, int *num) {
    void *d = dpy ? dpy : g_egl_dpy;
    if (peglChooseConfig && d) {
        unsigned int r = peglChooseConfig(d, attr, cfgs, sz, num);
        if (r == EGL_TRUE && num && *num > 0) return EGL_TRUE;
    }
    if (cfgs && sz > 0 && g_egl_cfg) cfgs[0] = g_egl_cfg;
    if (num) *num = g_egl_cfg ? 1 : 0;
    return g_egl_cfg ? EGL_TRUE : EGL_FALSE;
}

static unsigned int br_eglGetConfigAttrib(void *dpy, void *cfg, int attr, int *val) {
    void *d = dpy ? dpy : g_egl_dpy;
    if (peglGetConfigAttrib && d && cfg)
        return peglGetConfigAttrib(d, cfg, attr, val);
    if (val) *val = 8;
    return EGL_TRUE;
}

static void *br_eglCreateWindowSurface(void *dpy, void *cfg, void *win, const int *attr) {
    (void)dpy; (void)cfg; (void)win; (void)attr;
    return g_egl_surf;
}

static void *br_eglCreatePbufferSurface(void *dpy, void *cfg, const int *attr) {
    if (peglCreatePbufferSurface && dpy && cfg) {
        void *s = peglCreatePbufferSurface(dpy, cfg, attr);
        if (s) return s;
    }
    return g_egl_surf;
}

static unsigned int br_eglDestroySurface(void *dpy, void *surf) {
    if (!surf || surf == g_egl_surf) return EGL_TRUE;
    return peglDestroySurface ? peglDestroySurface(dpy ? dpy : g_egl_dpy, surf) : EGL_TRUE;
}

static void *br_eglCreateContext(void *dpy, void *cfg, void *share, const int *attr) {
    if (!g_ctx_claimed && g_egl_ctx) {
        g_ctx_claimed = 1;
        return g_egl_ctx;
    }
    if (peglCreateContext)
        return peglCreateContext(dpy ? dpy : g_egl_dpy, cfg ? cfg : g_egl_cfg, share, attr);
    return g_egl_ctx;
}

static unsigned int br_eglDestroyContext(void *dpy, void *ctx) {
    if (!ctx || ctx == g_egl_ctx) return EGL_TRUE;
    return peglDestroyContext ? peglDestroyContext(dpy ? dpy : g_egl_dpy, ctx) : EGL_TRUE;
}

static unsigned int br_eglMakeCurrent(void *dpy, void *draw, void *read, void *ctx) {
    void *d = dpy ? dpy : g_egl_dpy;
    if (ctx == EGL_NO_CONTEXT)
        return peglMakeCurrent ? peglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) : EGL_TRUE;
    void *ds = draw ? draw : g_egl_surf;
    void *rs = read ? read : g_egl_surf;
    if (peglMakeCurrent)
        return peglMakeCurrent(d, ds, rs, ctx ? ctx : g_egl_ctx);
    return EGL_TRUE;
}

static void *br_eglGetCurrentContext(void) {
    return peglGetCurrentContext ? peglGetCurrentContext() : g_egl_ctx;
}

static void *br_eglGetCurrentSurface(int readdraw) {
    return peglGetCurrentSurface ? peglGetCurrentSurface(readdraw) : g_egl_surf;
}

static unsigned int br_eglQuerySurface(void *dpy, void *surf, int attr, int *val) {
    if (attr == EGL_WIDTH && val)  { *val = g_win_w; return EGL_TRUE; }
    if (attr == EGL_HEIGHT && val) { *val = g_win_h; return EGL_TRUE; }
    if (peglQuerySurface)
        return peglQuerySurface(dpy ? dpy : g_egl_dpy, surf ? surf : g_egl_surf, attr, val);
    return EGL_TRUE;
}

static unsigned int br_eglSurfaceAttrib(void *dpy, void *surf, int attr, int val) {
    return peglSurfaceAttrib ? peglSurfaceAttrib(dpy ? dpy : g_egl_dpy, surf ? surf : g_egl_surf, attr, val) : EGL_TRUE;
}

static unsigned int br_eglSwapBuffers(void *dpy, void *surf) {
    (void)dpy; (void)surf;
    if (g_sdl_win && pSDL_GL_SwapWindow)
        pSDL_GL_SwapWindow(g_sdl_win);
    return EGL_TRUE;
}

static unsigned int br_eglSwapInterval(void *dpy, int interval) {
    (void)dpy;
    if (pSDL_GL_SetSwapInterval) pSDL_GL_SetSwapInterval(interval);
    return EGL_TRUE;
}

static void *br_eglGetProcAddress(const char *procname) {
    if (!procname) return NULL;
    for (unsigned i = 0; i < sizeof(g_gles_thunks) / sizeof(g_gles_thunks[0]); i++) {
        if (strcmp(procname, g_gles_thunks[i].name) == 0)
            return g_gles_thunks[i].fn;
    }
    return resolve_gles(procname);
}

/* ── ANativeWindow + ALooper + ASensor for M2 ──────────────────────────── */

static void *br_ANativeWindow_fromSurface(void *env, void *surface) {
    (void)env; (void)surface;
    return &g_native_win;
}
static void br_ANativeWindow_acquire(void *win) { (void)win; }
static void br_ANativeWindow_release(void *win) { (void)win; }
static int32_t br_ANativeWindow_getWidth(void *win) {
    (void)win; return g_win_w;
}
static int32_t br_ANativeWindow_getHeight(void *win) {
    (void)win; return g_win_h;
}
static int32_t br_ANativeWindow_setBuffersGeometry(void *win, int32_t w, int32_t h, int32_t fmt) {
    (void)win;
    if (w > 0 && h > 0) {
        g_native_win.width = w;
        g_native_win.height = h;
    }
    g_native_win.format = fmt;
    return 0;
}

static int g_dummy_looper = 1;
static int g_dummy_sensor_mgr = 1;
static int g_dummy_sensor_queue = 1;

static void *br_ALooper_prepare(int opts) { (void)opts; return &g_dummy_looper; }
static void *br_ALooper_forThread(void) { return &g_dummy_looper; }
static void  br_ALooper_acquire(void *l) { (void)l; }
static void  br_ALooper_release(void *l) { (void)l; }
static void  br_ALooper_wake(void *l) { (void)l; }
static int   br_ALooper_pollAll(int ms, int *fd, int *ev, void **data) {
    (void)ms;
    if (fd) *fd = -1;
    if (ev) *ev = 0;
    if (data) *data = NULL;
    return -3;
}

static void *br_ASensorManager_getInstance(void) { return &g_dummy_sensor_mgr; }
static const void *br_ASensorManager_getDefaultSensor(void *m, int t) { (void)m; (void)t; return NULL; }
static int br_ASensorManager_getSensorList(void *m, const void *const **l) { (void)m; if (l) *l = NULL; return 0; }
static void *br_ASensorManager_createEventQueue(void *m, void *l, int id, void *cb, void *d) {
    (void)m; (void)l; (void)id; (void)cb; (void)d; return &g_dummy_sensor_queue;
}
static int br_ASensorManager_destroyEventQueue(void *m, void *q) { (void)m; (void)q; return 0; }
static int br_ASensorEventQueue_enableSensor(void *q, const void *s) { (void)q; (void)s; return 0; }
static int br_ASensorEventQueue_disableSensor(void *q, const void *s) { (void)q; (void)s; return 0; }
static int br_ASensorEventQueue_setEventRate(void *q, const void *s, int32_t u) { (void)q; (void)s; (void)u; return 0; }
static int br_ASensorEventQueue_hasEvents(void *q) { (void)q; return 0; }
static int br_ASensorEventQueue_getEvents(void *q, void *e, size_t c) { (void)q; (void)e; (void)c; return 0; }
static const char *br_ASensor_getName(const void *s) { (void)s; return "none"; }
static const char *br_ASensor_getVendor(const void *s) { (void)s; return "none"; }
static int br_ASensor_getType(const void *s) { (void)s; return 0; }
static SOFTFP float br_ASensor_getResolution(const void *s) { (void)s; return 0.01f; }
static int br_ASensor_getMinDelay(const void *s) { (void)s; return 10000; }

static const so_default_dynlib g_sdl_egl_dynlib[] = {
    { "ALooper_prepare",                  (uintptr_t)br_ALooper_prepare },
    { "ALooper_forThread",                (uintptr_t)br_ALooper_forThread },
    { "ALooper_acquire",                  (uintptr_t)br_ALooper_acquire },
    { "ALooper_release",                  (uintptr_t)br_ALooper_release },
    { "ALooper_wake",                     (uintptr_t)br_ALooper_wake },
    { "ALooper_pollAll",                  (uintptr_t)br_ALooper_pollAll },

    { "ANativeWindow_fromSurface",        (uintptr_t)br_ANativeWindow_fromSurface },
    { "ANativeWindow_acquire",            (uintptr_t)br_ANativeWindow_acquire },
    { "ANativeWindow_release",            (uintptr_t)br_ANativeWindow_release },
    { "ANativeWindow_getWidth",           (uintptr_t)br_ANativeWindow_getWidth },
    { "ANativeWindow_getHeight",          (uintptr_t)br_ANativeWindow_getHeight },
    { "ANativeWindow_setBuffersGeometry", (uintptr_t)br_ANativeWindow_setBuffersGeometry },

    { "ASensorManager_getInstance",       (uintptr_t)br_ASensorManager_getInstance },
    { "ASensorManager_getDefaultSensor",  (uintptr_t)br_ASensorManager_getDefaultSensor },
    { "ASensorManager_getSensorList",     (uintptr_t)br_ASensorManager_getSensorList },
    { "ASensorManager_createEventQueue",  (uintptr_t)br_ASensorManager_createEventQueue },
    { "ASensorManager_destroyEventQueue", (uintptr_t)br_ASensorManager_destroyEventQueue },
    { "ASensorEventQueue_enableSensor",   (uintptr_t)br_ASensorEventQueue_enableSensor },
    { "ASensorEventQueue_disableSensor",  (uintptr_t)br_ASensorEventQueue_disableSensor },
    { "ASensorEventQueue_setEventRate",   (uintptr_t)br_ASensorEventQueue_setEventRate },
    { "ASensorEventQueue_hasEvents",      (uintptr_t)br_ASensorEventQueue_hasEvents },
    { "ASensorEventQueue_getEvents",      (uintptr_t)br_ASensorEventQueue_getEvents },
    { "ASensor_getName",                  (uintptr_t)br_ASensor_getName },
    { "ASensor_getVendor",                (uintptr_t)br_ASensor_getVendor },
    { "ASensor_getType",                  (uintptr_t)br_ASensor_getType },
    { "ASensor_getResolution",            (uintptr_t)br_ASensor_getResolution },
    { "ASensor_getMinDelay",              (uintptr_t)br_ASensor_getMinDelay },

    { "eglGetDisplay",                    (uintptr_t)br_eglGetDisplay },
    { "eglInitialize",                    (uintptr_t)br_eglInitialize },
    { "eglTerminate",                     (uintptr_t)br_eglTerminate },
    { "eglGetError",                      (uintptr_t)br_eglGetError },
    { "eglQueryString",                   (uintptr_t)br_eglQueryString },
    { "eglChooseConfig",                  (uintptr_t)br_eglChooseConfig },
    { "eglGetConfigAttrib",               (uintptr_t)br_eglGetConfigAttrib },
    { "eglCreateWindowSurface",           (uintptr_t)br_eglCreateWindowSurface },
    { "eglCreatePbufferSurface",          (uintptr_t)br_eglCreatePbufferSurface },
    { "eglDestroySurface",                (uintptr_t)br_eglDestroySurface },
    { "eglCreateContext",                 (uintptr_t)br_eglCreateContext },
    { "eglDestroyContext",                (uintptr_t)br_eglDestroyContext },
    { "eglMakeCurrent",                   (uintptr_t)br_eglMakeCurrent },
    { "eglGetCurrentContext",             (uintptr_t)br_eglGetCurrentContext },
    { "eglGetCurrentSurface",             (uintptr_t)br_eglGetCurrentSurface },
    { "eglQuerySurface",                  (uintptr_t)br_eglQuerySurface },
    { "eglSurfaceAttrib",                 (uintptr_t)br_eglSurfaceAttrib },
    { "eglSwapBuffers",                   (uintptr_t)br_eglSwapBuffers },
    { "eglSwapInterval",                  (uintptr_t)br_eglSwapInterval },
    { "eglGetProcAddress",                (uintptr_t)br_eglGetProcAddress },
};

const so_default_dynlib *sdl_egl_get_dynlib(int *out_bytes) {
    if (out_bytes) *out_bytes = (int)sizeof(g_sdl_egl_dynlib);
    return g_sdl_egl_dynlib;
}
