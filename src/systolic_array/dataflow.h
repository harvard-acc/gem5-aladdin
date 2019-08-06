#ifndef __SYSTOLIC_ARRAY_DATAFLOW_H__
#define __SYSTOLIC_ARRAY_DATAFLOW_H__

#include <vector>

#include "sim/ticked_object.hh"

#include "params/SystolicArray.hh"
#include "debug/SystolicDataflow.hh"
#include "fetch.h"
#include "commit.h"
#include "pe.h"

namespace systolic {

class SystolicArray;

class Dataflow : public Ticked {
 public:
  Dataflow(SystolicArray& _accel, const SystolicArrayParams& params);

  ~Dataflow() {
    for (auto pe : peArray)
      delete pe;
    for (auto fetch : inputFetchUnits)
      delete fetch;
    for (auto fetch : weightFetchUnits)
      delete fetch;
  }

  void setParams() {
    weightFoldBarrier = 0;
    doneCount = 0;
    state = Prefill;
    for (auto fetch : inputFetchUnits)
      fetch->setParams();
    for (auto fetch : weightFetchUnits)
      fetch->setParams();
    for (auto commit : commitUnits)
      commit->setParams();
  }

  void regStats() {
    Ticked::regStats();
    for (auto& commit : commitUnits)
      commit->regStats();
  }

  // Schedule data streaming event for event fetch unit. The streaming event of
  // a fetch unit is scheduled one cycle later than the one ahead of it.
  void scheduleStreamingEvents();

  void evaluate() override;

  void releaseBarrier() {
    weightFoldBarrier = 0;
    // Start streaming in data for the next weight fold.
    scheduleStreamingEvents();
  }

  void arriveWeightFoldBarrier() {
    weightFoldBarrier++;
    DPRINTF(SystolicDataflow,
            "Weight fold barrier, arrived: %d.\n",
            weightFoldBarrier);
    // If all fetch units have arrived at the barriers, clear the
    // barriers and schedule streaming events for the next weight fold.
    if (weightFoldBarrier == inputFetchUnits.size() + weightFetchUnits.size()) {
      DPRINTF(
          SystolicDataflow, "All have arrived at the weight fold barrier.\n");
      if (state == Compute)
        releaseBarrier();
    }
  }

  void notifyDone();

 protected:
  int peIndex(int r, int c) const;

  // The states of the dataflow. Idle means the systolic array doesn't have work
  // assigned to it, Prefill is the state when the fetch units are prefilling
  // its FIFO queues to the PE array, while the computation has not started.
  // After the prefilling is done, the state will be changed to Compute.
  enum State { Idle, Prefill, Compute };
  SystolicArray& accel;
  State state;

 public:
  std::vector<ProcElem*> peArray;
  std::vector<InputFetch*> inputFetchUnits;
  std::vector<WeightFetch*> weightFetchUnits;
  std::vector<Commit*> commitUnits;
  int weightFoldBarrier;
  int doneCount;
};

}  // namespace systolic

#endif
