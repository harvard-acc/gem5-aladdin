#ifndef __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_CONNECTION_H__
#define __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_CONNECTION_H__

#include <stdbool.h>
#include <stdlib.h>
#include "systolic_array_params.h"

#ifdef __cplusplus
extern "C" {
#endif

// Invoke the systolic array and block until it finishes.
void invokeSystolicArrayAndBlock(int accelerator_id,
                                 systolic_array_params_t data);

// Invoke the systolic array and return without blocking.
volatile int* invokeSystolicArrayAndReturn(
    int accelerator_id, systolic_array_params_t systolic_data);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif
