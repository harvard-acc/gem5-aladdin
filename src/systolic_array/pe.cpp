#include "systolic_array.h"
#include "pe.h"

namespace systolic {

void MulAccUnit::checkEndOfWindow() {
  // Check if this is the end of a convolutional window.
  const std::vector<int>& weightIndices = input1->indices;
  if (weightIndices[1] == accel.weightRows - 1 &&
      weightIndices[2] == accel.weightCols - 1 &&
      weightIndices[3] == accel.weightChans - 1) {
    output->windowEnd = true;
    output->bubble = false;
  }
}

// We can't directly do float16 operations, here we use a FP16 library for that.
template <>
void MulAccUnit::doMulAcc<float16>() {
  float16 input0Data = *(input0->getDataPtr<float16>());
  float16 input1Data = *(input1->getDataPtr<float16>());
  float16 input2Data = (input2->isWindowEnd() || input2->size() == 0)
                           ? 0
                           : *(input2->getDataPtr<float16>());
  output->resize(input0->size());
  float16* outputData = output->getDataPtr<float16>();
  *outputData = fp16(fp32(input0Data) * fp32(input1Data) + fp32(input2Data));
  std::vector<int>& inputIndices = input0->indices;
  std::vector<int>& weightIndices = input1->indices;
  DPRINTF(SystolicPE,
          "IReg (%d, %d, %d, %d): %f, WReg (%d, %d, %d, %d): %f, OReg: %f.\n",
          inputIndices[0], inputIndices[1], inputIndices[2], inputIndices[3],
          fp32(input0Data), weightIndices[0], weightIndices[1],
          weightIndices[2], weightIndices[3], fp32(input1Data),
          fp32(input2Data));
}

void MulAccUnit::evaluate() {
  // Only perform the MACC operation if the input and weight registers do not
  // contain bubbles.
  if (!input0->isBubble() && !input1->isBubble()) {
    if (accel.dataType == Int32)
      doMulAcc<int>();
    else if (accel.dataType == Int64)
      doMulAcc<int64_t>();
    else if (accel.dataType == Float16)
      doMulAcc<float16>();
    else if (accel.dataType == Float32)
      doMulAcc<float>();
    else if (accel.dataType == Float64)
      doMulAcc<double>();
    checkEndOfWindow();
  }
}

}  // namespace systolic
