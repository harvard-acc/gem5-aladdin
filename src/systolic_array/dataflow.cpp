#include "systolic_array.h"
#include "dataflow.h"

namespace systolic {

Dataflow::Dataflow(SystolicArray& _accel, const SystolicArrayParams& params)
    : Ticked(_accel, &(_accel.numCycles)), accel(_accel), state(Idle),
      peArray(params.peArrayRows * params.peArrayCols),
      inputFetchUnits(params.peArrayRows), weightFetchUnits(params.peArrayCols),
      commitUnits(params.peArrayRows), weightFoldBarrier(0), doneCount(0) {
  // Create PEs.
  for (int i = 0; i < peArray.size(); i++) {
    peArray[i] =
        new ProcElem(params.acceleratorName + ".pe" + std::to_string(i), accel);
  }
  // Form the pipeline by chaining the PEs.
  for (int r = 0; r < params.peArrayRows; r++) {
    for (int c = 0; c < params.peArrayCols; c++) {
      // Connect the input register to the one in the next PE down the row.
      if (c < params.peArrayCols - 1) {
        peArray[peIndex(r, c)]->output0 =
            peArray[peIndex(r, c + 1)]->inputReg.input();
      }
      // Connect the weight register to the one in the next PE down the column.
      if (r < params.peArrayRows - 1) {
        peArray[peIndex(r, c)]->output1 =
            peArray[peIndex(r + 1, c)]->weightReg.input();
      }
    }
  }

  // Create input fetch units.
  for (int i = 0; i < inputFetchUnits.size(); i++) {
    inputFetchUnits[i] = new InputFetch(
        i, accel, params, peArray[peIndex(i, 0)]->inputReg.input());
  }
  // Create weight fetch units.
  for (int i = 0; i < weightFetchUnits.size(); i++) {
    weightFetchUnits[i] = new WeightFetch(
        i, accel, params, peArray[peIndex(0, i)]->weightReg.input());
  }

  // Create output commit units. Every commit unit serves for a row of PEs.
  for (int i = 0; i < commitUnits.size(); i++) {
    commitUnits[i] = new Commit(i, accel, params);
    // Connect output registers of this PE row to the commit unit.
    for (int j = 0; j < params.peArrayCols; j++)
      commitUnits[i]->inputs[j] = peArray[peIndex(i, j)]->outputReg.output();
  }
}

int Dataflow::peIndex(int r, int c) const {
  assert(r < accel.peArrayRows && c < accel.peArrayCols &&
         "Out of bounds of the PE array.");
  return r * accel.peArrayCols + c;
}

void Dataflow::scheduleStreamingEvents() {
  for (int i = 0; i < inputFetchUnits.size(); i++)
    accel.schedule(inputFetchUnits[i]->startStreamingEvent,
                   accel.clockEdge(Cycles(i + 1)));
  for (int i = 0; i < weightFetchUnits.size(); i++)
    accel.schedule(weightFetchUnits[i]->startStreamingEvent,
                   accel.clockEdge(Cycles(i + 1)));
}

void Dataflow::notifyDone() {
  if (++doneCount == commitUnits.size()) {
    DPRINTF(SystolicDataflow, "Done :)\n");
    state = Idle;
    accel.notifyDone();
  }
}

void Dataflow::evaluate() {
  DPRINTF(SystolicDataflow, "%s\n", __func__);
  // Fetch unit operations. Do we need to fetch inputs/weights or/and pump
  // data to the PEs in this cycle?
  for (auto fetch : inputFetchUnits)
    fetch->evaluate();
  for (auto fetch : weightFetchUnits)
    fetch->evaluate();
  for (auto commit : commitUnits)
    commit->evaluate();

  if (state == Prefill) {
    // If all fetch units' queues are filled, schedule a start streaming event
    // for each one.
    bool prefillDone = true;
    for (const auto& fetch : inputFetchUnits)
      prefillDone &= fetch->isUnused() || fetch->filled();
    for (const auto& fetch : weightFetchUnits)
      prefillDone &= fetch->isUnused() || fetch->filled();
    if (prefillDone) {
      DPRINTF(SystolicDataflow, "Prefilling done.\n");
      // Schedule streaming event for every fetch unit.
      scheduleStreamingEvents();
      state = Compute;
    }
  } else if (state == Compute) {
    // Perform the computation for every PE.
    for (auto pe : peArray)
      pe->evaluate();

    // Update the registers after the computation.
    for (auto pe : peArray) {
      pe->inputReg.evaluate();
      pe->weightReg.evaluate();
      pe->outputReg.evaluate();
    }
  }
}

}  // namespace systolic
