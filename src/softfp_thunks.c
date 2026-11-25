/* softfp_thunks.c -- Soft-float (AAPCS) -> Hard-float (AAPCS-VFP) wrappers.
 *
 * Android armeabi-v7a libraries pass float/double values in core registers
 * (r0-r3) and on the stack. dArkOS arm-linux-gnueabihf glibc/libm expects them
 * in VFP registers (s0-s15 / d0-d7).
 *
 * Compiling these wrappers with __attribute__((pcs("aapcs"))) asks GCC to
 * accept softfp arguments/return values and convert them when calling hardfp
 * host functions.
 */

#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "softfp_thunks.h"

#ifdef __arm__
#define SOFTFP __attribute__((pcs("aapcs"), noinline))
#else
#define SOFTFP
#endif

static SOFTFP float  th_acosf(float x)               { return acosf(x); }
static SOFTFP float  th_asinf(float x)               { return asinf(x); }
static SOFTFP float  th_atan2f(float y, float x)     { return atan2f(y, x); }
static SOFTFP float  th_atanf(float x)               { return atanf(x); }
static SOFTFP double th_ceil(double x)               { return ceil(x); }
static SOFTFP float  th_ceilf(float x)               { return ceilf(x); }
static SOFTFP float  th_cosf(float x)                { return cosf(x); }
static SOFTFP float  th_exp2f(float x)               { return exp2f(x); }
static SOFTFP float  th_expf(float x)                { return expf(x); }
static SOFTFP double th_floor(double x)              { return floor(x); }
static SOFTFP float  th_floorf(float x)              { return floorf(x); }
static SOFTFP double th_fmax(double x, double y)     { return fmax(x, y); }
static SOFTFP float  th_fmaxf(float x, float y)      { return fmaxf(x, y); }
static SOFTFP float  th_fminf(float x, float y)      { return fminf(x, y); }
static SOFTFP double th_fmod(double x, double y)     { return fmod(x, y); }
static SOFTFP float  th_fmodf(float x, float y)      { return fmodf(x, y); }
static SOFTFP double th_ldexp(double x, int exp)     { return ldexp(x, exp); }
static SOFTFP float  th_ldexpf(float x, int exp)     { return ldexpf(x, exp); }
static SOFTFP float  th_log10f(float x)              { return log10f(x); }
static SOFTFP double th_modf(double x, double *iptr) { return modf(x, iptr); }
static SOFTFP float  th_modff(float x, float *iptr)  { return modff(x, iptr); }
static SOFTFP float  th_nearbyintf(float x)          { return nearbyintf(x); }
static SOFTFP double th_pow(double x, double y)      { return pow(x, y); }
static SOFTFP float  th_powf(float x, float y)       { return powf(x, y); }
static SOFTFP float  th_remainderf(float x, float y) { return remainderf(x, y); }
static SOFTFP float  th_rintf(float x)               { return rintf(x); }
static SOFTFP double th_round(double x)              { return round(x); }
static SOFTFP float  th_roundf(float x)              { return roundf(x); }
static SOFTFP double th_scalbn(double x, int n)      { return scalbn(x, n); }
static SOFTFP float  th_sinf(float x)                { return sinf(x); }
static SOFTFP float  th_sqrtf(float x)               { return sqrtf(x); }
static SOFTFP float  th_tanf(float x)                { return tanf(x); }
static SOFTFP float  th_truncf(float x)              { return truncf(x); }

static SOFTFP double th_difftime(time_t t1, time_t t0)       { return difftime(t1, t0); }
static SOFTFP double th_strtod(const char *s, char **endptr) { return strtod(s, endptr); }
static SOFTFP double th_atof(const char *s)                  { return atof(s); }

static const so_default_dynlib g_softfp_thunks[] = {
    { "acosf",      (uintptr_t)th_acosf },
    { "asinf",      (uintptr_t)th_asinf },
    { "atan2f",     (uintptr_t)th_atan2f },
    { "atanf",      (uintptr_t)th_atanf },
    { "ceil",       (uintptr_t)th_ceil },
    { "ceilf",      (uintptr_t)th_ceilf },
    { "cosf",       (uintptr_t)th_cosf },
    { "exp2f",      (uintptr_t)th_exp2f },
    { "expf",       (uintptr_t)th_expf },
    { "floor",      (uintptr_t)th_floor },
    { "floorf",     (uintptr_t)th_floorf },
    { "fmax",       (uintptr_t)th_fmax },
    { "fmaxf",      (uintptr_t)th_fmaxf },
    { "fminf",      (uintptr_t)th_fminf },
    { "fmod",       (uintptr_t)th_fmod },
    { "fmodf",      (uintptr_t)th_fmodf },
    { "ldexp",      (uintptr_t)th_ldexp },
    { "ldexpf",     (uintptr_t)th_ldexpf },
    { "log10f",     (uintptr_t)th_log10f },
    { "modf",       (uintptr_t)th_modf },
    { "modff",      (uintptr_t)th_modff },
    { "nearbyintf", (uintptr_t)th_nearbyintf },
    { "pow",        (uintptr_t)th_pow },
    { "powf",       (uintptr_t)th_powf },
    { "remainderf", (uintptr_t)th_remainderf },
    { "rintf",      (uintptr_t)th_rintf },
    { "round",      (uintptr_t)th_round },
    { "roundf",     (uintptr_t)th_roundf },
    { "scalbn",     (uintptr_t)th_scalbn },
    { "sinf",       (uintptr_t)th_sinf },
    { "sqrtf",      (uintptr_t)th_sqrtf },
    { "tanf",       (uintptr_t)th_tanf },
    { "truncf",     (uintptr_t)th_truncf },
    { "difftime",   (uintptr_t)th_difftime },
    { "strtod",     (uintptr_t)th_strtod },
    { "atof",       (uintptr_t)th_atof },
};

const so_default_dynlib *softfp_get_thunks(int *out_bytes) {
    if (out_bytes) *out_bytes = (int)sizeof(g_softfp_thunks);
    return g_softfp_thunks;
}
