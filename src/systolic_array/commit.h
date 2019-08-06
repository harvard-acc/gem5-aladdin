#ifndef __SYSTOLIC_ARRAY_COMMIT_H__
#define __SYSTOLIC_ARRAY_COMMIT_H__

#include <deque>

#include "params/SystolicArray.hh"
#include "debug/SystolicCommit.hh"
#include "tensor.h"
#include "register.h"
#include "datatypes.h"

// Each commit unit is responsible for collecting finished output pixels from a
// PE row and then write them to the output scratchpad. Once any output pixel is
// ready, the commit unit will collect it and buffer it until it has enough
// data for a writeback. A commit queue is used to buffer the writeback
// requests.

namespace systolic {

class SystolicArray;

class Commit : public LocalSpadInterface {
 public:
  Commit(int _id, SystolicArray& _accel, const SystolicArrayParams& params);

  void setParams();

  void regStats();

  void evaluate() override;

 protected:
  // Callback from the scratchpad port upon receiving a response.
  void localSpadCallback(PacketPtr pkt) override;

  // Check if we have collected all the output data in the specified line.
  bool isLineComplete(int lineIndex);

  // Create a writeback request and queue it to the commit queue.
  void queueCommitRequest(int lineIndex);

  int id;

  SystolicArray& accel;

  // The buffer that stores the data collected from PEs before they are written
  // back to the scratchpad.
  std::vector<PixelData> outputBuffer;

  // Number of elements per line.
  // TODO: make it static.
  int elemsPerLine;

  // True if this commit unit is not used at all due to a lack of work
  // available.
  bool unused;

  // True if all the data has been sent.
  bool allSent;

  // Number of outstanding commit requests.
  int numOutstandingReq;

  int remainingWeightFolds;

  // The queue stores the lines that are waiting to be sent to the scratchpad.
  std::deque<PacketPtr> commitQueue;
  int commitQueueCapacity;

  // The tensor iterator which provides the current commit address.
  TensorRegionIndexIterator iter;

  // The peak size the commit queue can get.
  Stats::Scalar commitQueuePeakSize;

 public:
  // The registers this commit unit is getting data from.
  std::vector<Register<PixelData>::IO> inputs;
};

}  // namespace systolic

#endif
