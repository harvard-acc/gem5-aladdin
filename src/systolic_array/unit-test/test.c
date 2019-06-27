#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "systolic_array_connection.h"
#include "aladdin/gem5/aladdin_sys_connection.h"

#define CACHELINE_SIZE 32

int main() {
  float *inputs, *weights, *outputs;
  int input_dims[4] = { 1, 32, 32, 8 };
  int weight_dims[4] = { 16, 3, 3, 8 };
  int output_dims[4] = { 1, 30, 30, 16 };
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
  for (int i = 0; i < input_size; i++)
    inputs[i] = 0;
  for (int i = 0; i < weight_size; i++)
    weights[i] = 0;

  systolic_array_data_t data;
  data.input_base_addr = &inputs[0];
  data.weight_base_addr = &weights[0];
  data.output_base_addr = &outputs[0];
  memcpy(data.input_dims, input_dims, sizeof(int) * 4);
  memcpy(data.weight_dims, weight_dims, sizeof(int) * 4);
  memcpy(data.output_dims, output_dims, sizeof(int) * 4);
  data.stride = 1;
  int accelerator_id = 4;
  mapArrayToAccelerator(
      accelerator_id, "", data.input_base_addr, input_size * sizeof(float));
  mapArrayToAccelerator(
      accelerator_id, "", data.weight_base_addr, weight_size * sizeof(float));
  mapArrayToAccelerator(
      accelerator_id, "", data.output_base_addr, output_size * sizeof(float));
  invokeSystolicArrayAndBlock(accelerator_id, data);
  free(inputs);
  free(weights);
  free(outputs);
  return 0;
}

