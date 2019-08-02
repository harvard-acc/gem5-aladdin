#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "systolic_array_connection.h"
#include "aladdin/gem5/aladdin_sys_connection.h"
#include "aladdin/gem5/aladdin_sys_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

void invokeSystolicArrayAndBlock(int accelerator_id,
                                 systolic_array_data_t systolic_data) {
  aladdin_params_t* params =
      (aladdin_params_t*)malloc(sizeof(aladdin_params_t));
  params->finish_flag = (volatile int*)malloc(sizeof(int));
  *(params->finish_flag) = NOT_COMPLETED;
  params->accel_params_ptr = &systolic_data;
  params->size = sizeof(systolic_array_data_t);
  ioctl(ALADDIN_FD, accelerator_id, params);
  while (*(params->finish_flag) == NOT_COMPLETED)
    ;
}

#ifdef __cplusplus
}  // extern "C"
#endif
