#include "systolic_array.h"
#include "commit.h"
#include "activations.h"

namespace systolic {

Commit::Commit(int _id,
               SystolicArray& _accel,
               const SystolicArrayParams& params)
    : LocalSpadInterface(
          _accel.name() + ".commit" + std::to_string(_id), _accel, params),
      id(_id), accel(_accel), elemsPerLine(_accel.lineSize / _accel.elemSize),
      unused(false), allSent(false), inputs(params.peArrayCols),
      outputBuffer(params.peArrayCols),
      commitQueueCapacity(params.commitQueueCapacity) {}

void Commit::setParams() {
  unused = false;
  allSent = false;
  remainingWeightFolds = accel.numWeightFolds;

  // Here we set the tensor iterator. Every weight fold will finish peArrayCols
  // of output feature maps, thus we first iterate over the region of a weight
  // fold in the output tensor (using the region iterator), and then advance the
  // region origin to the next weight fold.

  // The shape of the output tensor.
  TensorShape shape(
      { 1, accel.outputRows, accel.outputCols, accel.numEffecKerns },
      accel.alignment);
  // Set the tensor iterator.
  iter = TensorRegionIndexIterator(
      shape,
      { 0, 0, 0, 0 },
      { 1, accel.outputRows, accel.outputCols, accel.peArrayCols });
  // Move the iterator to the correct starting place.
  iter += { 0, 0, 0, accel.lineSize / accel.elemSize * id };
  // If the iterator reaches the end of the tensor, then this commit unit should
  // be left idle through the whole execution.
  if (iter.end())
    unused = true;
  DPRINTF(SystolicCommit, "Iterator initial indices: %s.\n", iter);
}

void Commit::regStats() {
  using namespace Stats;
  commitQueuePeakSize
      .name(name() + ".commitQueuePeakSize")
      .desc("The peak size that the commit queue can get.")
      .flags(total | nonan);
}

void Commit::evaluate() {
  // We will never see finished data available if this commit unit is unused.
  if (unused)
    return;

  // Collect any finished output pixel from the output register of the PEs.
  // Since the writeback granularity is a line, if we have collected every
  // output pixel forming the line, create a commit request and queue it to the
  // commit queue to be sent.
  //
  // There are two cases where the commit unit will never see some of output
  // pixels ready: 1) The commit unit is not used at all, which means the whole
  // PE row is left idle. 2) Some of the PE columns are left idle due to a lack
  // of weights. In this case, we should do a writeback once all the "active"
  // columns have produced outputs.
  for (int i = 0; i < inputs.size() / elemsPerLine; i++) {
    for (int j = 0; j < elemsPerLine; j++) {
      int index = i * elemsPerLine + j;
      if (inputs[index]->isWindowEnd()) {
        assert(!outputBuffer[index].isWindowEnd() &&
               "A new output pixel finished while the previous one from the "
               "same PE has not been written back.");
        // Collect the output pixel and store it in the local buffer.
        outputBuffer[index] = *inputs[index];
        DPRINTF(
            SystolicCommit, "Collected output data from column %d.\n", index);
      }
    }
    // Check if we have collected all the pixels for a writeback.
    if (isLineComplete(i))
      queueCommitRequest(i);
  }

  // Send requests from the commit queue if there are requests waiting
  // to be sent to the output scratchpad.
  auto queueIt = commitQueue.rbegin();
  while (!commitQueue.empty() && !localSpadPort.isStalled() &&
         queueIt != commitQueue.rend()) {
    // If the item at the front of queue has received the ack from the
    // scratchpad, we can delete it now.
    if (queueIt == commitQueue.rbegin() && (*queueIt)->acked) {
      delete *queueIt;
      commitQueue.pop_front();
      if (commitQueue.empty() && allSent) {
        DPRINTF(SystolicCommit, "All the output data has been written back.\n");
        accel.dataflow->notifyDone();
        break;
      }
    }

    if (!(*queueIt)->sent) {
      if (!localSpadPort.sendTimingReq((*queueIt)->pkt))
        DPRINTF(SystolicCommit, "Failed to send commit request. Will retry.\n");
      else
        DPRINTF(SystolicCommit, "Sent commit request.\n");
      (*queueIt)->sent = true;
    }
    queueIt++;
  }
}

bool Commit::isLineComplete(int lineIndex) {
  // Check if every slot in the local output buffer has been filled with
  // finished output. We also take the last weight fold into account, where some
  // PE columns can be left idle, thus the corresponding slot in the local
  // buffer will never see finished data.
  for (int i = lineIndex * elemsPerLine; i < (lineIndex + 1) * elemsPerLine;
       i++) {
    // We have idle PE columns in the last weight fold if the number of weights
    // is non-multiples of peArrayCols.
    bool haveIdleColumns = remainingWeightFolds == 1 &&
                           accel.numEffecKerns % accel.peArrayCols != 0;
    // Determine if this PE column is idle.
    bool isIdleColumn =
        haveIdleColumns && i >= accel.numEffecKerns % accel.peArrayCols;
    if (!outputBuffer[i].isWindowEnd() && !isIdleColumn)
      return false;
  }
  return true;
}

template <>
void Commit::accumOutputs(float16* currOutputs, float16* prevOutputs) {
  for (int i = 0; i < elemsPerLine; i++)
    currOutputs[i] = fp16(fp32(currOutputs[i]) + fp32(prevOutputs[i]));
}

void Commit::localSpadCallback(PacketPtr pkt) {
  DPRINTF(SystolicCommit, "Received response, addr %#x.\n", pkt->getAddr());
  CommitSenderState* state = pkt->findNextSenderState<CommitSenderState>();
  LineData* lineSlotPtr = state->getCommitQueueSlotPtr();
  if (pkt->isRead()) {
    // We got the previous partial sums. Now add it with the current output.
    if (accel.dataType == Int32) {
      accumOutputs<int>(lineSlotPtr->getDataPtr<int>(), pkt->getPtr<int>());
    } else if (accel.dataType == Int64) {
      accumOutputs<int64_t>(
          lineSlotPtr->getDataPtr<int64_t>(), pkt->getPtr<int64_t>());
    } else if (accel.dataType == Float16) {
      accumOutputs<float16>(
          lineSlotPtr->getDataPtr<float16>(), pkt->getPtr<float16>());
    } else if (accel.dataType == Float32) {
      accumOutputs<float>(
          lineSlotPtr->getDataPtr<float>(), pkt->getPtr<float>());
    } else if (accel.dataType == Float64) {
      accumOutputs<double>(
          lineSlotPtr->getDataPtr<double>(), pkt->getPtr<double>());
    }
    lineSlotPtr->deletePacket();
    if (accel.sendResults) {
      // If the outputs are finished, do the activation function before we send
      // the outputs back to the scratchpad.
      activationFunc(lineSlotPtr->getDataPtr<uint8_t>(),
                     elemsPerLine,
                     accel.actType,
                     accel.actParams,
                     accel.dataType);
    }
    // Send the write request.
    auto req = std::make_shared<Request>(
        pkt->getAddr(), accel.lineSize, 0, localSpadMasterId);
    req->setContext(accel.getContextId());
    PacketPtr pkt = new Packet(req, MemCmd::WriteReq);
    pkt->dataDynamic(lineSlotPtr->getDataPtr<uint8_t>());
    CommitSenderState* state = new CommitSenderState(lineSlotPtr);
    pkt->pushSenderState(state);
    // Set the packet pointer to the new write request.
    lineSlotPtr->pkt = pkt;
  } else {
    // Mark that the line has the data acked.
    lineSlotPtr->acked = true;
  }
}

void Commit::queueCommitRequest(int lineIndex) {
  Addr addr = iter * accel.elemSize;
  uint8_t* data = new uint8_t[accel.lineSize]();
  // Copy data from the buffer for the collected data.
  for (int i = 0; i < elemsPerLine; i++) {
    if (!outputBuffer[lineIndex * elemsPerLine + i].isBubble()) {
      memcpy(&data[i * accel.elemSize],
             outputBuffer[lineIndex * elemsPerLine + i].getDataPtr<uint8_t>(),
             accel.elemSize);
    }
  }
  PacketPtr pkt = nullptr;
  LineData* line = nullptr;
  if (accel.accumResults) {
    // If we need to accumulate results, read the previous results first.
    auto req =
        std::make_shared<Request>(addr, accel.lineSize, 0, localSpadMasterId);
    req->setContext(accel.getContextId());
    pkt = new Packet(req, MemCmd::ReadReq);
    pkt->allocate();
    line = new LineData(pkt, data);
  } else {
    if (accel.sendResults) {
      // If the outputs are finished, do the activation function before we send
      // the outputs back to the scratchpad.
      activationFunc(
          data, elemsPerLine, accel.actType, accel.actParams, accel.dataType);
    }
    // Directly write to the scratchpad if we don't need to accumulate the
    // results.
    auto req =
        std::make_shared<Request>(addr, accel.lineSize, 0, localSpadMasterId);
    req->setContext(accel.getContextId());
    pkt = new Packet(req, MemCmd::WriteReq);
    pkt->dataDynamic(data);
    line = new LineData(pkt);
  }
  DPRINTF(SystolicCommit, "Created a commit request at indices %s.\n", iter);

  commitQueue.push_back(line);
  if (commitQueue.size() >= commitQueueCapacity)
    warn("Commit queue exceeds its capacity after pushing new request. "
         "Current size: %d, capacity: %d.\n",
         commitQueue.size(), commitQueueCapacity);
  if (commitQueue.size() > commitQueuePeakSize.value())
      commitQueuePeakSize = commitQueue.size();
  CommitSenderState* state = new CommitSenderState(commitQueue.back());
  pkt->pushSenderState(state);

  // Clear the line in output buffer.
  for (int i = lineIndex * elemsPerLine; i < (lineIndex + 1) * elemsPerLine;
       i++) {
    outputBuffer[i].clear();
  }

  iter += { 0, 0, accel.peArrayRows, 0 };
  if (iter.end()) {
    // We have finished a weight fold. Arrive at the barrier. Move the iterator
    // to the next weight fold.
    iter.advanceOriginByStride({ 0, 0, 0, accel.peArrayCols });
    remainingWeightFolds--;
    if (iter.end()) {
      // We have finished all the weight folds.
      allSent = true;
    } else {
      // Move the iterator to the correct starting place for the next weight
      // fold.
      iter += { 0, 0, 0, accel.lineSize / accel.elemSize * id };
      DPRINTF(SystolicCommit, "Advanced iterator to %s.\n", iter);
    }
  }
}

}  // namespace systolic
