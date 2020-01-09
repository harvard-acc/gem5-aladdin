#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "systolic_array_connection.h"
#include "aladdin_sys_connection.h"
#include "aladdin_sys_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

void invokeSystolicArrayAndBlock(int accelerator_id,
                                 systolic_array_params_t systolic_data) {
  aladdin_params_t* params = getParams(
      NULL, NOT_COMPLETED, &systolic_data, sizeof(systolic_array_params_t));
  ioctl(ALADDIN_FD, accelerator_id, params);
  suspendCPUUntilFlagChanges(params->finish_flag);
  free((void*)(params->finish_flag));
  free(params);
}

volatile int* invokeSystolicArrayAndReturn(
    int accelerator_id, systolic_array_params_t systolic_data) {
  aladdin_params_t* params = getParams(
      NULL, NOT_COMPLETED, &systolic_data, sizeof(systolic_array_params_t));
  ioctl(ALADDIN_FD, accelerator_id, params);
  volatile int* finish_flag = params->finish_flag;
  free(params);
  return finish_flag;
}

#ifdef __cplusplus
}  // extern "C"
#endif
