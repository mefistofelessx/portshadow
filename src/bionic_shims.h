#ifndef BIONIC_SHIMS_H
#define BIONIC_SHIMS_H

#include "so_util.h"

/* Initialize internal shim state (stack guard, bootstrap locks). */
void bionic_shims_init(void);

/* Set extra dynlib tables used by our dlopen/dlsym shim (e.g. NDK/EGL stubs). */
void bionic_shims_set_extra_tables(const so_default_dynlib *extra1, int extra1_bytes,
                                   const so_default_dynlib *extra2, int extra2_bytes);

/* Returns pointer to the Bionic->glibc shim table and its byte size. */
const so_default_dynlib *bionic_get_shims(int *out_bytes);

#endif /* BIONIC_SHIMS_H */
