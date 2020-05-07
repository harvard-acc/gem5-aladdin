#ifndef __SYSTOLIC_ARRAY_FETCH_H__
#define __SYSTOLIC_ARRAY_FETCH_H__

#include <deque>

#include "params/SystolicArray.hh"
#include "debug/SystolicFetch.hh"
#include "local_spad_interface.h"
#include "tensor.h"
#include "register.h"
#include "datatypes.h"

namespace systolic {

class SystolicArray;

// A fetch unit reads data from the local scratchpad and feeds data to the
// connected PE.
class Fetch : public LocalSpadInterface {
 public:
  Fetch(const std::string& name,
        int _id,
        SystolicArray& _accel,
        const SystolicArrayParams& params,
        Register<PixelData>::IO _output);
  virtual ~Fetch() {}

 protected:
  // This represents a line of data we have fetched. For convenience, instead of
  // copying the data from the packet, we store the packet pointer.
  class LineData {
   public:
    LineData(PacketPtr _pkt,
             std::vector<int> _indices,
             bool _weightFoldEnd,
             bool _halo = false)
        : pkt(_pkt), indices(_indices), weightFoldEnd(_weightFoldEnd),
          halo(_halo), dataReturned(false) {}

    ~LineData() {
      if (pkt != nullptr) {
        delete pkt->popSenderState();
        delete pkt;
      }
    }

    std::vector<int> getIndices() const { return indices; }

    bool isWeightFoldEnd() const { return weightFoldEnd; }

    bool inHalo() const { return halo; }

    void markDataReturned() { dataReturned = true; }

    bool valid() const { return halo || dataReturned; }

    template <typename T>
    T* getDataPtr() {
      return pkt->getPtr<T>();
    }

   protected:
    PacketPtr pkt;
    // The indices of this line in the original tensor.
    std::vector<int> indices;
    bool weightFoldEnd;
    bool halo;
    bool dataReturned;
  };

  class FetchSenderState : public Packet::SenderState {
   public:
    FetchSenderState(LineData* _fetchQueueSlotPtr)
        : Packet::SenderState(), fetchQueueSlotPtr(_fetchQueueSlotPtr) {}

    LineData* getFetchQueueSlotPtr() const { return fetchQueueSlotPtr; }

   protected:
    // This points to the line we have reserved in the fetch queue for this
    // request.
    LineData* fetchQueueSlotPtr;
  };

 public:
  virtual void setParams() {
    fetchQueue.clear();
    pixelIndex = 0;
    weightFoldEnd = false;
    unused = false;
    allFetched = false;
    allConsumed = false;
    arrivedBarrier = true;
  }

  bool filled() const { return fetchQueue.size() == fetchQueueCapacity; }

  bool isUnused() const { return unused; }

  // Start data streaming.
  void startStreaming();

  void evaluate() override;

 protected:
  void localSpadCallback(PacketPtr pkt) override;

  // Send a read request to the local scratchpad.
  void fetch();

  // Adjust the tensor index iterator after sending a fetch request. Depending
  // on the tensor type the derived fetch unit is fetching, this should be
  // implemented accordingly.
  virtual void advanceTensorIter() = 0;

  // Each fetch unit is given a different ID, which monotonically increases from
  // 0. It is used to determine where to start the fetching. For example, the
  // fetch unit with ID N will start the fetching from the N-th convolution
  // window.
  int id;

  SystolicArray& accel;

  // The register this fetch unit is feeding into.
  Register<PixelData>::IO output;

  // The queue stores the lines fetched from the scratchpad.
  std::deque<LineData*> fetchQueue;
  int fetchQueueCapacity;

  // The line that is currently feeding the PE.
  LineData* feedingLine;
  int pixelIndex;

  // True if the window iterator has reached the end of the weight fold.
  bool weightFoldEnd;

  // True if this fetch unit is left idle for the whole invocation due to not
  // enough work.
  bool unused;

  // True if all the data has been fetched, while they may not have all been
  // streamed out to the PE array.
  bool allFetched;

  // True if all the data has been sent to the PE array.
  bool allConsumed;

  // True if the fetch unit has arrived at the weight fold barrier.
  bool arrivedBarrier;

  // Data dimensions of every fetch request.
  std::vector<int> fetchDims;
  // Original indices in the tensor data this fetch unit starts fetching from.
  std::vector<int> origIndices;
  // The stride of next window with respect to the current one.
  std::vector<int> windowStride;

  // The tensor iterator which provides the fetch address.
  TensorRegionIndexIterator tensorIter;

 public:
  EventFunctionWrapper startStreamingEvent;
};

class InputFetch: public Fetch {
 public:
  InputFetch(int id,
             SystolicArray& accel,
             const SystolicArrayParams& params,
             Register<PixelData>::IO output);

  void setParams() override;

 protected:
  void advanceTensorIter() override;

  // The input fetch unit needs to know how many weight folds there are, and
  // therefore starts over the input fetching that many times.
  int remainingWeightFolds;

  int finishedOutputFolds;
};

class WeightFetch: public Fetch {
 public:
  WeightFetch(int id,
              SystolicArray& accel,
              const SystolicArrayParams& params,
              Register<PixelData>::IO output);

  void setParams() override;

 protected:
  void advanceTensorIter() override;

  // The weight fetch unit needs to know, in contrast, how many output folds
  // there are, and therefore starts over the weight fetching that many times.
  int remainingOutputFolds;

  int finishedWeightFolds;
};

}  // namespace systolic

#endif
