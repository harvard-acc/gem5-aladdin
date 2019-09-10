#ifndef __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_PARAMS_H__
#define __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_PARAMS_H__

#ifdef __cplusplus
extern "C" {
#endif

// Struct of custom accelerator parameters. The user program uses this struct to
// pass runtime parameters.
typedef struct _systolic_array_params_t {
  void* input_base_addr;
  void* weight_base_addr;
  void* output_base_addr;
  int input_dims[4];
  int weight_dims[4];
  int output_dims[4];
  int stride;
  int input_halo_pad[4];
  bool send_results;
} systolic_array_params_t;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif

