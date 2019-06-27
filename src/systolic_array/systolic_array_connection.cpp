#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "systolic_array_connection.h"
#include "aladdin/gem5/aladdin_sys_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

void invokeSystolicArrayAndBlock(int accelerator_id,
                                 systolic_array_data_t systolic_data) {
  volatile int* finish_flag = (volatile int*)malloc(sizeof(int));
  *finish_flag = NOT_COMPLETED;
  systolic_data.finish_flag = finish_flag;
  ioctl(ALADDIN_FD, accelerator_id, &systolic_data);
  while (*finish_flag == NOT_COMPLETED)
    ;
  free((void*)finish_flag);
}

#ifdef __cplusplus
}  // extern "C"
#endif
