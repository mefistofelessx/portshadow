/* ndk_stubs.c -- Minimal Android NDK (ALooper/ANativeWindow/ASensor) & EGL stubs for M1. */

#include <stdint.h>
#include <stdio.h>
#include "ndk_stubs.h"

#ifdef __arm__
#define SOFTFP __attribute__((pcs("aapcs")))
#else
#define SOFTFP
#endif

static int g_dummy_looper = 1;
static int g_dummy_sensor_mgr = 1;
static int g_dummy_sensor_queue = 1;

/* ── ALooper ───────────────────────────────────────────────────────────── */
static void *stub_ALooper_prepare(int opts) { (void)opts; return &g_dummy_looper; }
static void *stub_ALooper_forThread(void) { return &g_dummy_looper; }
static void  stub_ALooper_acquire(void *looper) { (void)looper; }
static void  stub_ALooper_release(void *looper) { (void)looper; }
static void  stub_ALooper_wake(void *looper) { (void)looper; }
static int   stub_ALooper_pollAll(int timeoutMillis, int *outFd, int *outEvents, void **outData) {
    (void)timeoutMillis;
    if (outFd) *outFd = -1;
    if (outEvents) *outEvents = 0;
    if (outData) *outData = NULL;
    return -3; /* ALOOPER_POLL_TIMEOUT */
}

/* ── ANativeWindow ─────────────────────────────────────────────────────── */
static void   *stub_ANativeWindow_fromSurface(void *env, void *surface) { (void)env; (void)surface; return NULL; }
static void    stub_ANativeWindow_acquire(void *win) { (void)win; }
static void    stub_ANativeWindow_release(void *win) { (void)win; }
static int32_t stub_ANativeWindow_getWidth(void *win) { (void)win; return 720; }
static int32_t stub_ANativeWindow_getHeight(void *win) { (void)win; return 720; }
static int32_t stub_ANativeWindow_setBuffersGeometry(void *win, int32_t w, int32_t h, int32_t fmt) {
    (void)win; (void)w; (void)h; (void)fmt; return 0;
}

/* ── ASensor ───────────────────────────────────────────────────────────── */
static void *stub_ASensorManager_getInstance(void) { return &g_dummy_sensor_mgr; }
static const void *stub_ASensorManager_getDefaultSensor(void *mgr, int type) { (void)mgr; (void)type; return NULL; }
static int stub_ASensorManager_getSensorList(void *mgr, const void *const **list) {
    (void)mgr;
    if (list) *list = NULL;
    return 0;
}
static void *stub_ASensorManager_createEventQueue(void *mgr, void *looper, int ident, void *cb, void *data) {
    (void)mgr; (void)looper; (void)ident; (void)cb; (void)data;
    return &g_dummy_sensor_queue;
}
static int stub_ASensorManager_destroyEventQueue(void *mgr, void *q) { (void)mgr; (void)q; return 0; }
static int stub_ASensorEventQueue_enableSensor(void *q, const void *s) { (void)q; (void)s; return 0; }
static int stub_ASensorEventQueue_disableSensor(void *q, const void *s) { (void)q; (void)s; return 0; }
static int stub_ASensorEventQueue_setEventRate(void *q, const void *s, int32_t usec) { (void)q; (void)s; (void)usec; return 0; }
static int stub_ASensorEventQueue_hasEvents(void *q) { (void)q; return 0; }
static int stub_ASensorEventQueue_getEvents(void *q, void *events, size_t count) { (void)q; (void)events; (void)count; return 0; }
static const char *stub_ASensor_getName(const void *s) { (void)s; return "stub"; }
static const char *stub_ASensor_getVendor(const void *s) { (void)s; return "stub"; }
static int stub_ASensor_getType(const void *s) { (void)s; return 0; }
static SOFTFP float stub_ASensor_getResolution(const void *s) { (void)s; return 0.01f; }
static int stub_ASensor_getMinDelay(const void *s) { (void)s; return 10000; }

/* ── EGL stubs (to be backed by SDL2 + Mali libEGL in M2) ──────────────── */
static void *stub_eglGetDisplay(void *dpy) { (void)dpy; return (void *)0x1; }
static unsigned int stub_eglInitialize(void *dpy, int *maj, int *min) {
    (void)dpy; if (maj) *maj = 1; if (min) *min = 4; return 1;
}
static unsigned int stub_eglTerminate(void *dpy) { (void)dpy; return 1; }
static int stub_eglGetError(void) { return 0x3000; /* EGL_SUCCESS */ }
static const char *stub_eglQueryString(void *dpy, int name) { (void)dpy; (void)name; return ""; }
static unsigned int stub_eglChooseConfig(void *dpy, const int *attr, void **configs, int sz, int *num) {
    (void)dpy; (void)attr; if (configs && sz > 0) configs[0] = (void *)0x1; if (num) *num = 1; return 1;
}
static unsigned int stub_eglGetConfigAttrib(void *dpy, void *cfg, int attr, int *val) {
    (void)dpy; (void)cfg; (void)attr; if (val) *val = 8; return 1;
}
static void *stub_eglCreateWindowSurface(void *dpy, void *cfg, void *win, const int *attr) {
    (void)dpy; (void)cfg; (void)win; (void)attr; return (void *)0x1;
}
static void *stub_eglCreatePbufferSurface(void *dpy, void *cfg, const int *attr) {
    (void)dpy; (void)cfg; (void)attr; return (void *)0x1;
}
static unsigned int stub_eglDestroySurface(void *dpy, void *surf) { (void)dpy; (void)surf; return 1; }
static void *stub_eglCreateContext(void *dpy, void *cfg, void *share, const int *attr) {
    (void)dpy; (void)cfg; (void)share; (void)attr; return (void *)0x1;
}
static unsigned int stub_eglDestroyContext(void *dpy, void *ctx) { (void)dpy; (void)ctx; return 1; }
static unsigned int stub_eglMakeCurrent(void *dpy, void *draw, void *read, void *ctx) {
    (void)dpy; (void)draw; (void)read; (void)ctx; return 1;
}
static void *stub_eglGetCurrentContext(void) { return NULL; }
static void *stub_eglGetCurrentSurface(int readdraw) { (void)readdraw; return NULL; }
static unsigned int stub_eglQuerySurface(void *dpy, void *surf, int attr, int *val) {
    (void)dpy; (void)surf; (void)attr; if (val) *val = 720; return 1;
}
static unsigned int stub_eglSurfaceAttrib(void *dpy, void *surf, int attr, int val) {
    (void)dpy; (void)surf; (void)attr; (void)val; return 1;
}
static unsigned int stub_eglSwapBuffers(void *dpy, void *surf) { (void)dpy; (void)surf; return 1; }
static unsigned int stub_eglSwapInterval(void *dpy, int interval) { (void)dpy; (void)interval; return 1; }
static void *stub_eglGetProcAddress(const char *procname) {
    (void)procname;
    return NULL;
}

static const so_default_dynlib g_ndk_stubs[] = {
    { "ALooper_prepare",                  (uintptr_t)stub_ALooper_prepare },
    { "ALooper_forThread",                (uintptr_t)stub_ALooper_forThread },
    { "ALooper_acquire",                  (uintptr_t)stub_ALooper_acquire },
    { "ALooper_release",                  (uintptr_t)stub_ALooper_release },
    { "ALooper_wake",                     (uintptr_t)stub_ALooper_wake },
    { "ALooper_pollAll",                  (uintptr_t)stub_ALooper_pollAll },

    { "ANativeWindow_fromSurface",        (uintptr_t)stub_ANativeWindow_fromSurface },
    { "ANativeWindow_acquire",            (uintptr_t)stub_ANativeWindow_acquire },
    { "ANativeWindow_release",            (uintptr_t)stub_ANativeWindow_release },
    { "ANativeWindow_getWidth",           (uintptr_t)stub_ANativeWindow_getWidth },
    { "ANativeWindow_getHeight",          (uintptr_t)stub_ANativeWindow_getHeight },
    { "ANativeWindow_setBuffersGeometry", (uintptr_t)stub_ANativeWindow_setBuffersGeometry },

    { "ASensorManager_getInstance",       (uintptr_t)stub_ASensorManager_getInstance },
    { "ASensorManager_getDefaultSensor",  (uintptr_t)stub_ASensorManager_getDefaultSensor },
    { "ASensorManager_getSensorList",     (uintptr_t)stub_ASensorManager_getSensorList },
    { "ASensorManager_createEventQueue",  (uintptr_t)stub_ASensorManager_createEventQueue },
    { "ASensorManager_destroyEventQueue", (uintptr_t)stub_ASensorManager_destroyEventQueue },
    { "ASensorEventQueue_enableSensor",   (uintptr_t)stub_ASensorEventQueue_enableSensor },
    { "ASensorEventQueue_disableSensor",  (uintptr_t)stub_ASensorEventQueue_disableSensor },
    { "ASensorEventQueue_setEventRate",   (uintptr_t)stub_ASensorEventQueue_setEventRate },
    { "ASensorEventQueue_hasEvents",      (uintptr_t)stub_ASensorEventQueue_hasEvents },
    { "ASensorEventQueue_getEvents",      (uintptr_t)stub_ASensorEventQueue_getEvents },
    { "ASensor_getName",                  (uintptr_t)stub_ASensor_getName },
    { "ASensor_getVendor",                (uintptr_t)stub_ASensor_getVendor },
    { "ASensor_getType",                  (uintptr_t)stub_ASensor_getType },
    { "ASensor_getResolution",            (uintptr_t)stub_ASensor_getResolution },
    { "ASensor_getMinDelay",              (uintptr_t)stub_ASensor_getMinDelay },

    { "eglGetDisplay",                    (uintptr_t)stub_eglGetDisplay },
    { "eglInitialize",                    (uintptr_t)stub_eglInitialize },
    { "eglTerminate",                     (uintptr_t)stub_eglTerminate },
    { "eglGetError",                      (uintptr_t)stub_eglGetError },
    { "eglQueryString",                   (uintptr_t)stub_eglQueryString },
    { "eglChooseConfig",                  (uintptr_t)stub_eglChooseConfig },
    { "eglGetConfigAttrib",               (uintptr_t)stub_eglGetConfigAttrib },
    { "eglCreateWindowSurface",           (uintptr_t)stub_eglCreateWindowSurface },
    { "eglCreatePbufferSurface",          (uintptr_t)stub_eglCreatePbufferSurface },
    { "eglDestroySurface",                (uintptr_t)stub_eglDestroySurface },
    { "eglCreateContext",                 (uintptr_t)stub_eglCreateContext },
    { "eglDestroyContext",                (uintptr_t)stub_eglDestroyContext },
    { "eglMakeCurrent",                   (uintptr_t)stub_eglMakeCurrent },
    { "eglGetCurrentContext",             (uintptr_t)stub_eglGetCurrentContext },
    { "eglGetCurrentSurface",             (uintptr_t)stub_eglGetCurrentSurface },
    { "eglQuerySurface",                  (uintptr_t)stub_eglQuerySurface },
    { "eglSurfaceAttrib",                 (uintptr_t)stub_eglSurfaceAttrib },
    { "eglSwapBuffers",                   (uintptr_t)stub_eglSwapBuffers },
    { "eglSwapInterval",                  (uintptr_t)stub_eglSwapInterval },
    { "eglGetProcAddress",                (uintptr_t)stub_eglGetProcAddress },
};

const so_default_dynlib *ndk_get_stubs(int *out_bytes) {
    if (out_bytes) *out_bytes = (int)sizeof(g_ndk_stubs);
    return g_ndk_stubs;
}
