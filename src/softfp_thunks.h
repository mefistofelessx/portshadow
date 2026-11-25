#ifndef SOFTFP_THUNKS_H
#define SOFTFP_THUNKS_H

#include "so_util.h"

/* Returns pointer to the softfp->hardfp thunk table and its byte size. */
const so_default_dynlib *softfp_get_thunks(int *out_bytes);

#endif /* SOFTFP_THUNKS_H */
