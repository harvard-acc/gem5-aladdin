#include "activations.h"
#include "utils.h"

namespace systolic {

template <>
void relu(float16* inputs, int elems) {
  for (int i = 0; i < elems; i++) {
    if (fp32(inputs[i]) < 0)
      inputs[i] = 0;
  }
}

template <>
void lrelu(float16* inputs, int elems, float slope) {
  for (int i = 0; i < elems; i++) {
    if (fp32(inputs[i]) < 0)
      inputs[i] = fp16(slope * fp32(inputs[i]));
  }
}

template <>
void elu(float16* inputs, int elems, float alpha) {
  for (int i = 0; i < elems; i++) {
    if (fp32(inputs[i]) < 0)
      inputs[i] = fp16(alpha * (exp(fp32(inputs[i])) - 1));
  }
}

template <>
void selu(float16* inputs, int elems, float alpha, float lambda) {
  elu<float16>(inputs, elems, alpha);
  for (int i = 0; i < elems; i++)
    inputs[i] = fp16(lambda * fp32(inputs[i]));
}

template <>
void sigmoid(float16* inputs, int elems) {
  for (int i = 0; i < elems; i++)
    inputs[i] = fp16(1.0 / (1.0 + exp(-fp32(inputs[i]))));
}

template <>
void tanh(float16* inputs, int elems) {
  for (int i = 0; i < elems; i++)
    inputs[i] = fp16(2 * fp32(inputs[i]));
  sigmoid<float16>(inputs, elems);
  for (int i = 0; i < elems; i++)
    inputs[i] = fp16(2 * fp32(inputs[i]) - 1);
}

template <>
void hardTanh(float16* inputs, int elems, float min, float max) {
  for (int i = 0; i < elems; i++) {
    inputs[i] = fp32(inputs[i]) < min
                    ? fp16(min)
                    : fp32(inputs[i]) > max ? fp16(max) : inputs[i];
  }
}

#define DEFINE_ACTIVATION_FUNC_DISPATCH(function)                              \
  template <typename... Args>                                                  \
  void function##Dispatch(                                                      \
      uint8_t* inputs, int elems, DataType dataType, Args... args) {           \
    if (dataType == Int32)                                                     \
      function<int>((int*)inputs, elems, args...);                             \
    else if (dataType == Int64)                                                \
      function<int64_t>((int64_t*)inputs, elems, args...);                     \
    else if (dataType == Float16)                                              \
      function<float16>((float16*)inputs, elems, args...);                     \
    else if (dataType == Float32)                                              \
      function<float>((float*)inputs, elems, args...);                         \
    else if (dataType == Float64)                                              \
      function<double>((double*)inputs, elems, args...);                       \
  }

DEFINE_ACTIVATION_FUNC_DISPATCH(relu)
DEFINE_ACTIVATION_FUNC_DISPATCH(lrelu)
DEFINE_ACTIVATION_FUNC_DISPATCH(elu)
DEFINE_ACTIVATION_FUNC_DISPATCH(selu)
DEFINE_ACTIVATION_FUNC_DISPATCH(tanh)
DEFINE_ACTIVATION_FUNC_DISPATCH(hardTanh)
DEFINE_ACTIVATION_FUNC_DISPATCH(sigmoid)

void activationFunc(uint8_t* inputs,
                    int elems,
                    systolic_activation_type function,
                    systolic_activation_params params,
                    DataType dataType) {
  if (function == SYSTOLIC_NO_ACTIVATION)
    return;
  else if (function == SYSTOLIC_RELU)
    reluDispatch(inputs, elems, dataType);
  else if (function == SYSTOLIC_LRELU)
    lreluDispatch(inputs, elems, dataType, params.slope);
  else if (function == SYSTOLIC_ELU)
    eluDispatch(inputs, elems, dataType, params.alpha);
  else if (function == SYSTOLIC_SELU)
    seluDispatch(inputs, elems, dataType, params.alpha, params.lambda);
  else if (function == SYSTOLIC_TANH)
    tanhDispatch(inputs, elems, dataType);
  else if (function == SYSTOLIC_HARD_TANH)
    hardTanhDispatch(inputs, elems, dataType, params.min, params.max);
  else if (function == SYSTOLIC_SIGMOID)
    sigmoidDispatch(inputs, elems, dataType);
  else if (function == SYSTOLIC_SOFTMAX)
    assert(false && "Softmax not added yet.");
  else
    assert(false && "Unknown activation function.");
}

}  // namespace systolic
