#ifndef __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_PARAMS_H__
#define __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_PARAMS_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _systolic_activation_type {
    SYSTOLIC_NO_ACTIVATION,
    SYSTOLIC_RELU,
    SYSTOLIC_RELU_THRESHOLD,
    SYSTOLIC_LRELU,
    SYSTOLIC_ELU,
    SYSTOLIC_SELU,
    SYSTOLIC_TANH,
    SYSTOLIC_HARD_TANH,
    SYSTOLIC_SIGMOID,
    SYSTOLIC_SOFTMAX
} systolic_activation_type;

typedef struct _systolic_activation_params {
    // LReLU
    float slope;
    // ELU/SELU
    float alpha;
    float lambda;
    // Hard Tanh
    float min;
    float max;
} systolic_activation_params;

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
  bool read_inputs;
  bool read_weights;
  bool send_results;
  systolic_activation_type act_type;
  systolic_activation_params act_params;
} systolic_array_params_t;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif

