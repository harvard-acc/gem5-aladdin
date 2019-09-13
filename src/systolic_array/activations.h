#ifndef __SYSTOLIC_ARRAY_ACTIVATIONS_H__
#define __SYSTOLIC_ARRAY_ACTIVATIONS_H__

#include <cmath>
#include "datatypes.h"
#include "systolic_array_params.h"

namespace systolic {

template <typename ElemType>
void relu(ElemType* inputs, int elems) {
  for (int i = 0; i < elems; i++) {
    if (inputs[i] < 0)
      inputs[i] = 0;
  }
}

template <typename ElemType>
void lrelu(ElemType* inputs, int elems, float slope) {
  for (int i = 0; i < elems; i++) {
    if (inputs[i] < 0)
      inputs[i] = slope * inputs[i];
  }
}

template <typename ElemType>
void elu(ElemType* inputs, int elems, float alpha) {
  for (int i = 0; i < elems; i++) {
    if (inputs[i] < 0)
      inputs[i] = alpha * (exp(inputs[i]) - 1);
  }
}

template <typename ElemType>
void selu(ElemType* inputs, int elems, float alpha, float lambda) {
  elu<ElemType>(inputs, elems, alpha);
  for (int i = 0; i < elems; i++)
    inputs[i] = lambda * inputs[i];
}

template <typename ElemType>
void sigmoid(ElemType* inputs, int elems) {
  for (int i = 0; i < elems; i++)
    inputs[i] = 1.0 / (1.0 + exp(-inputs[i]));
}

template <typename ElemType>
void tanh(ElemType* inputs, int elems) {
  for (int i = 0; i < elems; i++)
    inputs[i] *= 2;
  sigmoid<ElemType>(inputs, elems);
  for (int i = 0; i < elems; i++)
    inputs[i] = 2 * inputs[i] - 1;
}

template <typename ElemType>
void hardTanh(ElemType* inputs, int elems, float min, float max) {
  for (int i = 0; i < elems; i++)
    inputs[i] = inputs[i] < min ? min : inputs[i] > max ? max : inputs[i];
}

void activationFunc(uint8_t* inputs,
                    int elems,
                    systolic_activation_type function,
                    systolic_activation_params params,
                    DataType dataType);

}  // namespace systolic

#endif
