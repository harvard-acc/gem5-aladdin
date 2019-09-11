#ifndef __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_PARAMS_H__
#define __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_PARAMS_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _activation_type {
    NO_ACTIVATION,
    RELU,
    RELU_THRESHOLD,
    LRELU,
    ELU,
    SELU,
    TANH,
    HARD_TANH,
    SIGMOID,
    SOFTMAX
} activation_type;

typedef struct _activation_params {
    // LReLU
    float slope;
    // ELU/SELU
    float alpha;
    float lambda;
    // Hard Tanh
    float min;
    float max;
} activation_params;

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
  int ifmap_start;
  int kern_start;
  bool accum_results;
  bool send_results;
  activation_type act_type;
  activation_params act_params;
} systolic_array_params_t;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif

