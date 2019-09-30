#include "systolic_array.h"
#include "fetch.h"

namespace systolic {

Fetch::Fetch(const std::string& name,
             int _id,
             SystolicArray& _accel,
             const SystolicArrayParams& params,
             Register<PixelData>::IO _output)
    : LocalSpadInterface(name, _accel, params), id(_id), accel(_accel),
      output(_output), unused(false), allFetched(false), allConsumed(false),
      arrivedBarrier(true), fetchQueueCapacity(params.fetchQueueCapacity),
      feedingLine(nullptr), pixelIndex(0), weightFoldEnd(false),
      fetchDims({ 0, 0, 0, _accel.lineSize / _accel.elemSize }),
      startStreamingEvent([this] { startStreaming(); }, "startStreamingEvent") {
}

void Fetch::startStreaming() {
  if (unused || allConsumed) {
    // If this fetch unit should be left idle or has finished all the work,
    // directly go to the barrier.
    arrivedBarrier = true;
    accel.dataflow->arriveWeightFoldBarrier();
  } else {
    arrivedBarrier = false;
  }
}

void Fetch::localSpadCallback(PacketPtr pkt) {
  DPRINTF(SystolicFetch, "Received response, addr %#x\n", pkt->getAddr());
  FetchSenderState* state = pkt->findNextSenderState<FetchSenderState>();
  auto lineSlotPtr = state->getFetchQueueSlotPtr();
  // Mark that the line has the data returned.
  lineSlotPtr->markDataReturned();
}

void Fetch::fetch() {
  // The address/indices of the current fetch request.
  DPRINTF(SystolicFetch, "Fetching at indices %s.\n", tensorIter);
  Addr addr = tensorIter * accel.elemSize;
  std::vector<int> indices = tensorIter.getIndices();
  bool inHaloRegion = tensorIter.inHaloRegion();
  // Change the tensor iterator for the next fetch.
  advanceTensorIter();

  // If we are in the halo regions, don't access the scratchpad and instead
  // construct a line of zeros.
  if (inHaloRegion) {
    LineData* line = new LineData(nullptr, indices, weightFoldEnd, true);
    fetchQueue.push_back(line);
    DPRINTF(SystolicFetch, "Constructed a line for halo regions.\n");
  } else {
    auto req =
        std::make_shared<Request>(addr, accel.lineSize, 0, localSpadMasterId);
    req->setContext(accel.getContextId());
    PacketPtr pkt = new Packet(req, MemCmd::ReadReq);
    pkt->allocate();
    // Reserve a line in the fetch queue.
    LineData* line = new LineData(pkt, indices, weightFoldEnd);
    fetchQueue.push_back(line);
    // Keep the pointer to the reserved line slot in sender state.
    FetchSenderState* state = new FetchSenderState(fetchQueue.back());
    pkt->pushSenderState(state);
    DPRINTF(SystolicFetch, "Fetching a line, addr %#x\n", addr);

    if (!localSpadPort.sendTimingReq(pkt))
      DPRINTF(SystolicFetch, "Sending fetch request, retrying.\n");
    else
      DPRINTF(SystolicFetch, "Sent fetch request.\n");

  }
}

void Fetch::evaluate() {
  // Here we evaluate two things: 1) Do we need to fetch more data from the
  // scratchpad? 2) Do we need to feed data to the PE array?

  DPRINTF(SystolicFetch,
          "Fetch queue occupied space: %d / %d, allFetched: %d, "
          "allConsumed: %d, arrived at barrier: %d.\n",
          fetchQueue.size(), fetchQueueCapacity, allFetched, allConsumed,
          arrivedBarrier);

  // No work to do if this fetch unit is not used at all or all the data has
  // been sent to the PE array.
  if (unused || allConsumed)
    return;

  // 1) Evaluate the fetching part.
  //
  // If we have remaining fetching work and the queue has reservable capacity,
  // then reserve one slot in the queue and send a read request to the
  // scratchpad.
  if (!allFetched && fetchQueue.size() < fetchQueueCapacity &&
      !localSpadPort.isStalled())
    fetch();

  // 2) Evaluate the feeding part.
  //
  // Don't stream out data if the fetch unit has arrived at the barrier.
  if (arrivedBarrier)
    return;

  // Pop a line from the queue if needed.
  if (feedingLine == nullptr || pixelIndex == accel.lineSize / accel.elemSize) {
    assert(!fetchQueue.empty() &&
           "Line queue becomes empty while streaming out data.");
    feedingLine = fetchQueue.front();
    fetchQueue.pop_front();
    pixelIndex = 0;
  }
  if (!feedingLine->valid()) {
    // Another case that the fetching can't keep pace with the feeding.
    fatal("Streaming out premature data!\n");
  }

  // Stream out data from the queue. One pixel at a time.
  output->resize(accel.elemSize);
  if (feedingLine->inHalo()) {
    output->clear();
  } else {
    memcpy(output->getDataPtr<uint8_t>(),
           feedingLine->getDataPtr<uint8_t>() + pixelIndex * accel.elemSize,
           accel.elemSize);
  }
  output->indices = feedingLine->getIndices();
  output->indices[3] += pixelIndex;
  output->bubble = false;
  if (++pixelIndex == accel.lineSize / accel.elemSize) {
    if (feedingLine->isWeightFoldEnd()) {
      // Arrive at the barrier if this is the last pixel of a weight fold.
      arrivedBarrier = true;
      accel.dataflow->arriveWeightFoldBarrier();
      if (allFetched && fetchQueue.empty())
        allConsumed = true;
    }
    delete feedingLine;
    feedingLine = nullptr;
  }
}

InputFetch::InputFetch(int id,
                       SystolicArray& accel,
                       const SystolicArrayParams& params,
                       Register<PixelData>::IO output)
    : Fetch(accel.name() + ".input_fetch" + std::to_string(id),
            id,
            accel,
            params,
            output),
      finishedOutputFolds(0) {}

void InputFetch::setParams() {
  Fetch::setParams();

  remainingWeightFolds = accel.numWeightFolds;
  finishedOutputFolds = 0;

  // The shape of the tensor this fetch unit is fetching from.
  TensorShape shape(
      { 1, accel.inputRows, accel.inputCols, accel.inputChans }, accel.alignment);
  // The halo regions around the input tensor.
  std::vector<std::pair<int, int>> halo{
    { 0, 0 },
    { accel.inputTopPad, accel.inputBottomPad },
    { accel.inputLeftPad, accel.inputRightPad },
    { 0, 0 }
  };

  // Set the stride.
  windowStride = std::vector<int>{ 0, 0, accel.peArrayRows, 0 };

  // Set the tensor iterator.
  tensorIter = TensorRegionIndexIterator(
      shape, halo,
      { 0, -accel.inputTopPad, -accel.inputLeftPad, accel.ifmapStart },
      { 1, accel.weightRows, accel.weightCols, accel.weightChans },
      { 1, accel.stride, accel.stride, 1 });
  // Set the original indices.
  tensorIter.advanceOriginByStride({ 0, 0, id, 0 });
  if (tensorIter.end()) {
    // This is the case where the number of output folds is smaller than the
    // PE row size, and such that some PEs will stay idle during the whole
    // execution.
    unused = true;
    return;
  }
  origIndices = tensorIter.getIndices();
  DPRINTF(SystolicFetch, "Tensor iterator initial indices: %s.\n", tensorIter);
}

void InputFetch::advanceTensorIter() {
  // Advance to the next place for subsequent fetch requests.
  tensorIter += fetchDims;
  weightFoldEnd = false;
  if (tensorIter.end()) {
    // We have finished a convolution window and need to move to the next.
    DPRINTF(SystolicFetch, "Finished output fold %d.\n", finishedOutputFolds);
    finishedOutputFolds++;
    tensorIter.advanceOriginByStride(windowStride);
    if (tensorIter.end()) {
      // We have finished the all the work for the current weight fold, so move
      // back to the starting origins for the next weight fold. Check if we have
      // remaining weight folds before doing that.
      DPRINTF(SystolicFetch, "Finished weight fold %d.\n",
              accel.numWeightFolds - remainingWeightFolds);
      // Before fetching data for the next weight fold, we need to barrier wait
      // with other fetch units.
      weightFoldEnd = true;
      if (--remainingWeightFolds > 0) {
        tensorIter.setOrigin(origIndices);
        finishedOutputFolds = 0;
        assert(!tensorIter.end() && "Window iterator should not reach the "
                                    "end after resetting the origin.");
      } else {
        DPRINTF(
            SystolicFetch, "All the required input data has been fetched.\n");
        allFetched = true;
      }
    }
  }
}

WeightFetch::WeightFetch(int id,
                         SystolicArray& accel,
                         const SystolicArrayParams& params,
                         Register<PixelData>::IO output)
    : Fetch(accel.name() + ".weight_fetch" + std::to_string(id),
            id,
            accel,
            params,
            output),
      finishedWeightFolds(0) {}

void WeightFetch::setParams() {
  Fetch::setParams();

  remainingOutputFolds = accel.numOutputFolds;
  finishedWeightFolds = 0;

  // The shape of the tensor this fetch unit is fetching from.
  TensorShape shape(
      { accel.numKerns, accel.weightRows, accel.weightCols, accel.weightChans },
      accel.alignment);

  // Set the stride.
  windowStride = std::vector<int>{ accel.peArrayCols, 0, 0, 0 };

  // Set the line index iterator within the window.
  tensorIter = TensorRegionIndexIterator(
      shape,
      { accel.kernStart, 0, 0, 0 },
      { 1, accel.weightRows, accel.weightCols, accel.weightChans });
  // Set the original indices.
  tensorIter.advanceOriginByStride({ id, 0, 0, 0 });
  if (tensorIter.end()) {
    // This is the case where the number of weights is smaller than the PE
    // column size, and such that some PEs will stay idle during the whole
    // execution.
    unused = true;
    return;
  }
  origIndices = tensorIter.getIndices();
  DPRINTF(SystolicFetch, "Tensor iterator initial indices: %s.\n", tensorIter);
}

void WeightFetch::advanceTensorIter() {
  // Advance to the next place for subsequent fetch requests.
  tensorIter += fetchDims;
  weightFoldEnd = false;
  if (tensorIter.end()) {
    DPRINTF(SystolicFetch, "Finished output fold %d\n",
            accel.numOutputFolds - remainingOutputFolds);
    // We have finished a convolution window and need to check if we have
    // remaining input folds.
    if (--remainingOutputFolds > 0) {
      // There are remaining input folds, so move the tensor iterators back to
      // the original places.
      tensorIter.setOrigin(origIndices);
      assert(!tensorIter.end() && "Window iterator should not reach the "
                                  "end after resetting the origin.");
    } else {
      // We need to move on to the next weight fold.
      DPRINTF(SystolicFetch, "Finished weight fold %d\n", finishedWeightFolds);
      finishedWeightFolds++;
      tensorIter.advanceOriginByStride(windowStride);
      // Before we start fetching the next weight fold, we need to barrier wait
      // other fetch units.
      weightFoldEnd = true;
      if (accel.numWeightFolds == finishedWeightFolds) {
        DPRINTF(
            SystolicFetch, "All the required weight data has been fetched.\n");
        allFetched = true;
      } else {
        remainingOutputFolds = accel.numOutputFolds;
        origIndices = tensorIter.getIndices();
      }
    }
  }
}

}  // namespace systolic
