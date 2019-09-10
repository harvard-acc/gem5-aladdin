#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "systolic_array_connection.h"
#include "aladdin/gem5/aladdin_sys_connection.h"

#define CACHELINE_SIZE 32

int main() {
  float *inputs, *weights, *outputs;
  int input_dims[4] = { 1, 16, 16, 8 };
  int weight_dims[4] = { 16, 3, 3, 8 };
  int output_dims[4] = { 1, 16, 16, 16 };
  int input_halo_pad[4] = { 1, 1, 1, 1 };
  int input_size = 1, weight_size = 1, output_size = 1;
  for (int i = 0; i < 4; i++) {
    input_size *= input_dims[i];
    weight_size *= weight_dims[i];
    output_size *= output_dims[i];
  }
  int err = posix_memalign(
      (void**)&inputs, CACHELINE_SIZE, sizeof(float) * input_size);
  assert(err == 0 && "Failed to allocate memory!");
  err = posix_memalign(
      (void**)&weights, CACHELINE_SIZE, sizeof(float) * weight_size);
  assert(err == 0 && "Failed to allocate memory!");
  err = posix_memalign(
      (void**)&outputs, CACHELINE_SIZE, sizeof(float) * output_size);
  assert(err == 0 && "Failed to allocate memory!");

  int reset_counter = input_dims[3];
  float value = 0;
  for (int i = 0; i < input_size; i++) {
    inputs[i] = value++;
    if ((i + 1) % reset_counter == 0)
      value = 0;
  }
  value = 0;
  reset_counter = weight_dims[3];
  for (int i = 0; i < weight_size; i++) {
    weights[i] = value++;
    if ((i + 1) % reset_counter == 0)
      value = 0;
  }

  systolic_array_params_t data;
  data.input_base_addr = &inputs[0];
  data.weight_base_addr = &weights[0];
  data.output_base_addr = &outputs[0];
  memcpy(data.input_dims, input_dims, sizeof(int) * 4);
  memcpy(data.weight_dims, weight_dims, sizeof(int) * 4);
  memcpy(data.output_dims, output_dims, sizeof(int) * 4);
  data.stride = 1;
  memcpy(data.input_halo_pad, input_halo_pad, sizeof(int) * 4);
  data.send_results = true;
  int accelerator_id = 4;
  mapArrayToAccelerator(
      accelerator_id, "", data.input_base_addr, input_size * sizeof(float));
  mapArrayToAccelerator(
      accelerator_id, "", data.weight_base_addr, weight_size * sizeof(float));
  mapArrayToAccelerator(
      accelerator_id, "", data.output_base_addr, output_size * sizeof(float));
  invokeSystolicArrayAndBlock(accelerator_id, data);

  for (int i = 0; i < output_size; i++) {
    printf("%.2f ", outputs[i]);
    if ((i + 1) % output_dims[3] == 0)
      printf("\n");
    if ((i + 1) % (output_dims[2] * output_dims[3]) == 0)
      printf("\n");
  }
  printf("\n");
  free(inputs);
  free(weights);
  free(outputs);
  return 0;
}

