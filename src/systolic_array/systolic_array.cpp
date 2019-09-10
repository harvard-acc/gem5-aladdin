#include "systolic_array.h"

namespace systolic {

void SystolicArray::processTick() {
  if (state == ReadyForDmaInputRead) {
    issueDmaInputRead();
    state = WaitingForDmaInputRead;
  } else if (state == ReadyForDmaWeightRead) {
    issueDmaWeightRead();
    state = WaitingForDmaWeightRead;
  } else if (state == ReadyToCompute) {
    dataflow->start();
    state = WaitingForCompute;
  } else if (state == ReadyForDmaWrite) {
    issueDmaWrite();
    state = WaitingForDmaWrite;
  } else if (state == ReadyToSendFinish) {
    sendFinishedSignal();
    state = Idle;
  }
  // If the accelerator is still busy, schedule the next tick.
  if (state != Idle && !tickEvent.scheduled())
    schedule(tickEvent, clockEdge(Cycles(1)));
}

void SystolicArray::issueDmaInputRead() {
  DPRINTF(SystolicToplevel, "Start DMA reads for inputs.\n");
  int inputSize = inputRows * inputCols * inputChans * elemSize;
  uint8_t* inputData = new uint8_t[inputSize]();
  SystolicDmaEvent* inputDmaEvent =
      new SystolicDmaEvent(this, inputBaseAddr, Input);
  splitAndSendDmaRequest(
      inputBaseAddr, inputSize, true, inputData, inputDmaEvent);
}

void SystolicArray::issueDmaWeightRead() {
  DPRINTF(SystolicToplevel, "Start DMA reads for weights.\n");
  int weightSize = weightRows * weightCols * weightChans * numKerns * elemSize;
  uint8_t* weightData = new uint8_t[weightSize]();
  auto weightDmaEvent = new SystolicDmaEvent(this, weightBaseAddr, Weight);
  splitAndSendDmaRequest(
      weightBaseAddr, weightSize, true, weightData, weightDmaEvent);
}

void SystolicArray::issueDmaWrite() {
  DPRINTF(SystolicToplevel, "Start DMA writes.\n");
  int outputSize = outputRows * outputCols * numOfmaps * elemSize;
  uint8_t* outputData = new uint8_t[outputSize]();
  outputSpad->accessData(0, outputSize, outputData, true);
  auto outputDmaEvent = new SystolicDmaEvent(this, outputBaseAddr, Output);
  splitAndSendDmaRequest(
      outputBaseAddr, outputSize, false, outputData, outputDmaEvent);
}

}  // namespace systolic

systolic::SystolicArray* SystolicArrayParams::create() {
  return new systolic::SystolicArray(this);
}

