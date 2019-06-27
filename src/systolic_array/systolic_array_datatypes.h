#ifndef __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_DATATYPES_H__
#define __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_DATATYPES_H__

// systolic data
typedef struct _systolic_array_data_t {
  volatile int* finish_flag;
  void* input_base_addr;
  void* weight_base_addr;
  void* output_base_addr;
  int input_dims[4];
  int weight_dims[4];
  int output_dims[4];
  int stride;
} systolic_array_data_t;

#endif
