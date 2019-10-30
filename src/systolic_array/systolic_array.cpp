#include "systolic_array.h"

namespace systolic {

bool SystolicArray::queueCommand(std::unique_ptr<AcceleratorCommand> cmd) {
  if (state != Idle) {
    // Queue the command if the systolic array is busy.
    DPRINTF(SystolicToplevel, "Queuing command %s on accelerator %d.\n",
            cmd->name(), accelerator_id);
    commandQueue.push_back(std::move(cmd));
  } else {
    // Directly run the command if the accelerator is not busy.
    cmd->run(this);
  }
  return true;
}

void SystolicArray::processTick() {
  if (state == ReadyForDmaInputRead) {
    issueDmaInputRead();
    state = WaitingForDmaInputRead;
  } else if (state == ReadyForDmaWeightRead) {
    issueDmaWeightRead();
    state = WaitingForDmaWeightRead;
  } else if (state == ReadyToCompute) {
    DPRINTF(SystolicToplevel, "Start compute.\n");
    dataflow->start();
    state = WaitingForCompute;
  } else if (state == ReadyForDmaWrite) {
    issueDmaWrite();
    state = WaitingForDmaWrite;
  } else if (state == ReadyToSendFinish) {
    sendFinishedSignal();
    state = WaitForFinishSignalAck;
  } else if (state == ReadyToWakeupCpu) {
    wakeupCpuThread();
    state = Idle;
    // If there are more commands, run them until we reach the next
    // ActivateAcceleratorCmd or the end of the queue.
    while (!commandQueue.empty()) {
      auto cmd = std::move(commandQueue.front());
      bool blocking = cmd->blocking();
      cmd->run(this);
      commandQueue.pop_front();
      if (blocking)
        break;
    }
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

