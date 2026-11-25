#ifndef SDL_EGL_H
#define SDL_EGL_H

#include "so_util.h"

/* Initialize SDL2 video + OpenGL ES 2.0 context and load Mali libEGL/libGLESv2. */
int sdl_egl_init(void);

/* Tear down SDL2 window and GL context. */
void sdl_egl_shutdown(void);

/* Pump SDL2 events; returns 1 if quit requested, 0 otherwise. */
int sdl_egl_poll_events(void);

/* Query active window dimensions. */
void sdl_egl_get_size(int *out_w, int *out_h);

/* Returns dynlib table providing real EGL/GLESv2 + ANativeWindow + ALooper/ASensor. */
const so_default_dynlib *sdl_egl_get_dynlib(int *out_bytes);

/* Returns our ANativeWindow handle representing the SDL2 window. */
void *sdl_egl_get_native_window(void);

#endif /* SDL_EGL_H */
