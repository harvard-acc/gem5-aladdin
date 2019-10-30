#ifndef __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_H__
#define __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_H__

#include <string>
#include <deque>
#include <iostream>
#include <algorithm>
#include <climits>
#include <fstream>
#include <sstream>

#include "base/logging.hh"
#include "base/statistics.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/system.hh"
#include "sim/eventq.hh"
#include "sim/clocked_object.hh"
#include "dev/dma_device.hh"
#include "aladdin/gem5/aladdin_tlb.hh"
#include "aladdin/gem5/Gem5Datapath.h"

#include "params/SystolicArray.hh"
#include "debug/SystolicToplevel.hh"
#include "systolic_array_params.h"
#include "dataflow.h"
#include "fetch.h"
#include "scratchpad.h"
#include "datatypes.h"

// This models a systolic array accelerator, which uses output stationary as its
// dataflow. The used data layout is NHWC. Three SRAMs of equal size are used to
// store inputs, kernels and outputs, respectively. The following diagram
// depicts the dataflow and the mapping from inputs/kernels/outputs to the PE
// arrays (a 4x4 array in this example). Assume that the input shape is (1, 32,
// 32, 8), the kernel shape is (16, 3, 3, 8), with stride size 1.
//
//                              |<----------Weight fold------------>|
//                                                          Kernel3
//                                                 Kernel2     |
//                                        Kernel1     |        |
//                               Kernel0     |        |        |
//                                  |        |        |        |
//                                  |        |        |        |
//                                  V        V        V        V
//   ^        InputWindow0 -->  |--PE00--|--PE01--|--PE02--|--PE03--|
// Output    InputWindow1 --->  |--PE04--|--PE05--|--PE06--|--PE07--|
// fold     InputWindow2 ---->  |--PE08--|--PE09--|--PE10--|--PE11--|
//   V     InputWindow3 ----->  |--PE12--|--PE13--|--PE14--|--PE15--|
//
// The inputs (read from local SRAM) are fed from left edge of the array and
// pumped towards the right edge, while the top edge streams in pixels from the
// different kerenls and pump them downwards. Here, every input window is the
// input pixels in a convolution (which generates a pixel in an output feature
// map), therefore in this case every input window is a region of (1, 3, 3, 8)
// shape. Every PE column is responsible for generating an output feature map,
// of which different PEs produces adjact output pixels in a single feathure
// map.
//
// As the output size and the kernel size can be larger than the PE array can
// fit, we partition them into 'folds' and iterate over them. In the above
// example, the output feature map size is 32x32=1024, which will be partitioned
// into 1024/4=256 folds. Similarly, the 16 kernels will be partitioned into
// 16/4=4 folds.

namespace systolic {

class SystolicArray : public Gem5Datapath {
 public:
  typedef SystolicArrayParams Params;
  SystolicArray(const Params* p)
      : Gem5Datapath(p,
                     p->acceleratorId,
                     p->maxDmaRequests,
                     p->dmaChunkSize,
                     p->numDmaChannels,
                     p->invalidateOnDmaStore,
                     p->system),
        tickEvent(this), state(Idle), peArrayRows(p->peArrayRows),
        peArrayCols(p->peArrayCols), lineSize(p->lineSize), alignment(8),
        dataType(UnknownDataType), elemSize(0), inputSpad(p->inputSpad),
        weightSpad(p->weightSpad), outputSpad(p->outputSpad) {
    setDataType(p->dataType);
    dataflow = new Dataflow(*this, *p);
    system->registerAccelerator(accelerator_id, this);
  }

  ~SystolicArray() {
    delete dataflow;
    system->deregisterAccelerator(accelerator_id);
  }

  Port& getPort(const std::string& if_name,
                PortID idx = InvalidPortID) override {
    if (if_name == "input_spad_port")
      return dataflow->inputFetchUnits[idx]->getLocalSpadPort();
    else if (if_name == "weight_spad_port")
      return dataflow->weightFetchUnits[idx]->getLocalSpadPort();
    else if (if_name == "output_spad_port")
      return dataflow->commitUnits[idx]->getLocalSpadPort();
    else
      return Gem5Datapath::getPort(if_name, idx);
  }

  void regStats() override {
    Gem5Datapath::regStats();
    using namespace Stats;
    numCycles
        .name(name() + ".numCycles")
        .desc("Total number of cycles.")
        .flags(total | nonan);
    dataflow->regStats();
  }

  // Returns the tick event that will schedule the next step.
  Event& getTickEvent() override { return tickEvent; }

  void setParams(std::unique_ptr<uint8_t[]> accel_params) override {
    systolic_array_params_t* accelParams =
        reinterpret_cast<systolic_array_params_t*>(accel_params.get());

    inputBaseAddr = (Addr)accelParams->input_base_addr;
    weightBaseAddr = (Addr)accelParams->weight_base_addr;
    outputBaseAddr = (Addr)accelParams->output_base_addr;
    inputRows = accelParams->input_dims[1];
    inputCols = accelParams->input_dims[2];
    inputChans = accelParams->input_dims[3];
    weightRows = accelParams->weight_dims[1];
    weightCols = accelParams->weight_dims[2];
    weightChans = accelParams->weight_dims[3];
    outputRows = accelParams->output_dims[1];
    outputCols = accelParams->output_dims[2];
    numOfmaps = accelParams->output_dims[3];
    numKerns = accelParams->weight_dims[0];
    // Number of effective kernels for this invocation. The weights can contain
    // more kernels than the number of ofmaps that the outputs scratchpad can
    // fit, where the number of effective kernels should be the number of
    // ofmaps.
    numEffecKerns = std::min(numKerns, numOfmaps);
    stride = accelParams->stride;
    inputTopPad = accelParams->input_halo_pad[0];
    inputBottomPad = accelParams->input_halo_pad[1];
    inputLeftPad = accelParams->input_halo_pad[2];
    inputRightPad = accelParams->input_halo_pad[3];
    ifmapStart = accelParams->ifmap_start;
    kernStart = accelParams->kern_start;
    accumResults = accelParams->accum_results;
    readInputs = accelParams->read_inputs;
    readWeights = accelParams->read_weights;
    sendResults = accelParams->send_results;
    actType = accelParams->act_type;
    actParams = accelParams->act_params;

    // Infer the numbers of folds needed to map the convolution to the PE array.
    numOutputFolds = ceil(outputRows * outputCols * 1.0 / peArrayRows);
    numWeightFolds = ceil(numEffecKerns * 1.0 / peArrayCols);

    DPRINTF(SystolicToplevel,
            "Convolution parameters: inputs (%d, %d, %d, %d), weights (%d, %d, "
            "%d, %d), outputs (%d, %d, %d, %d), stride %d, input halo padding "
            "(%d, %d, %d, %d), ifmap start %d, kernel start %d, accumulate "
            "results %d, read inputs %d, read weights %d, send results %d, "
            "output folds %d, weight folds %d.\n",
            accelParams->input_dims[0], inputRows, inputCols, inputChans,
            numKerns, weightRows, weightCols, weightChans,
            accelParams->output_dims[0], outputRows, outputCols, numOfmaps,
            stride, inputTopPad, inputBottomPad, inputLeftPad, inputRightPad,
            ifmapStart, kernStart, accumResults, readInputs, readWeights,
            sendResults, numOutputFolds, numWeightFolds);

    dataflow->setParams();
  }

  bool queueCommand(std::unique_ptr<AcceleratorCommand> cmd) override;

  void initializeDatapath(int delay) override {
    assert(state == Idle &&
           "The systolic array accelerator is not idle!");
    // Read inputs/weights if we need to.
    if (readInputs)
      state = ReadyForDmaInputRead;
    else if (readWeights)
      state = ReadyForDmaWeightRead;
    else
      state = ReadyToCompute;
    // Start running the accelerator.
    scheduleOnEventQueue(delay);
  }

  void sendFinishedSignal() override {
    Request::Flags flags = 0;
    int size = 4;  // 32 bit integer.
    uint8_t* data = new uint8_t[size];
    // Set some sentinel value.
    for (int i = 0; i < size; i++)
      data[i] = 0x13;
    auto req =
        std::make_shared<Request>(finish_flag, size, flags, cacheMasterId);
    req->setContext(context_id);  // Only needed for prefetching.
    MemCmd::Command cmd = MemCmd::WriteReq;
    PacketPtr pkt = new Packet(req, cmd);
    pkt->dataStatic<uint8_t>(data);
    SystolicSenderState* state = new SystolicSenderState(true);
    pkt->pushSenderState(state);

    if (!cachePort.sendTimingReq(pkt)) {
      assert(!cachePort.inRetry());
      cachePort.setRetryPkt(pkt);
      DPRINTF(SystolicToplevel, "Sending finished signal failed, retrying.\n");
    } else {
      DPRINTF(SystolicToplevel, "Sent finished signal.\n");
    }
  }

  void insertTLBEntry(Addr vaddr, Addr paddr) override {
    DPRINTF(
        SystolicToplevel, "Mapping vaddr 0x%x -> paddr 0x%x.\n", vaddr, paddr);
    Addr vpn = vaddr & ~(pageMask());
    Addr ppn = paddr & ~(pageMask());
    DPRINTF(SystolicToplevel,
            "Inserting TLB entry vpn 0x%x -> ppn 0x%x.\n",
            vpn,
            ppn);
    tlb.insert(vpn, ppn);
  }

  void insertArrayLabelToVirtual(const std::string& array_label,
                                 Addr vaddr,
                                 size_t size) override {}

  Addr getBaseAddress(std::string label) override {
    assert(false && "Should not call this for the systolic array accelerator!");
  }

  void resetTrace() override {
    assert(false && "Should not call this for the systolic array accelerator!");
  }

  void processTick();

  void notifyDone() {
    assert(state = WaitingForCompute);
    dataflow->stop();
    if (sendResults)
      state = ReadyForDmaWrite;
    else
      state = ReadyToSendFinish;
  }

 protected:
  enum State {
    Idle,
    ReadyForDmaInputRead,
    WaitingForDmaInputRead,
    ReadyForDmaWeightRead,
    WaitingForDmaWeightRead,
    ReadyToCompute,
    WaitingForCompute,
    ReadyForDmaWrite,
    WaitingForDmaWrite,
    ReadyToSendFinish,
    WaitForFinishSignalAck,
    ReadyToWakeupCpu,
  };

  enum TensorType { Input, Weight, Output };

  class SystolicDmaEvent : public DmaEvent {
   public:
    SystolicDmaEvent(SystolicArray* datapath,
                     Addr startAddr,
                     TensorType _tensorType)
        : DmaEvent(datapath, startAddr), tensorType(_tensorType) {}
    SystolicDmaEvent(const SystolicDmaEvent& other)
        : DmaEvent(other.datapath, other.startAddr),
          tensorType(other.tensorType) {}
    const char* description() const override { return "SystolicDmaEvent"; }
    SystolicDmaEvent* clone() const override {
      return new SystolicDmaEvent(*this);
    }
    TensorType getTensorType() const { return tensorType; }

   protected:
    TensorType tensorType;
  };

  class SystolicSenderState : public Packet::SenderState {
   public:
    SystolicSenderState(bool _is_ctrl_signal)
        : is_ctrl_signal(_is_ctrl_signal) {}

    /* Flag that determines whether a packet received on a data port is a
     * control signal accessed through memory (which needs to be handled
     * differently) or an ordinary memory access.
     */
    bool is_ctrl_signal;
  };

  void dmaRespCallback(PacketPtr pkt) override {
    SystolicDmaEvent* event =
        dynamic_cast<SystolicDmaEvent*>(DmaPort::getPacketCompletionEvent(pkt));
    // If it's a DMA read response, fill the data into the local scratchpad.
    if (pkt->isRead()) {
      // Since the address in the packet is the physical address, we need the
      // virtual address in order to access the local scratchpad.
      Addr paddr = pkt->getAddr();
      Addr paddrBase = DmaPort::getPacketAddr(pkt);
      Addr pageOffset = paddr - paddrBase;
      Addr pktOffset = pageOffset + event->getReqOffset();
      if (event->getTensorType() == Input) {
        inputSpad->accessData(
            pktOffset, pkt->getSize(), pkt->getPtr<uint8_t>(), false);
      } else if (event->getTensorType() == Weight) {
        weightSpad->accessData(
            pktOffset, pkt->getSize(), pkt->getPtr<uint8_t>(), false);
      }
    }
  }

  void dmaCompleteCallback(DmaEvent* event) override {
    if (state == WaitingForDmaInputRead) {
      DPRINTF(SystolicToplevel, "Completed DMA reads for inputs.\n");
      // Skip reading the weights if the scratchpad already has data filled.
      if (readWeights)
        state = ReadyForDmaWeightRead;
      else
        state = ReadyToCompute;
    } else if (state == WaitingForDmaWeightRead) {
      DPRINTF(SystolicToplevel, "Completed DMA reads for weights.\n");
      state = ReadyToCompute;
    } else if (state == WaitingForDmaWrite) {
      DPRINTF(SystolicToplevel, "Completed all DMA writes.\n");
      state = ReadyToSendFinish;
    }
  }

  virtual void cacheRespCallback(PacketPtr pkt) override {
    if (state == WaitForFinishSignalAck) {
      SystolicSenderState* senderState =
          pkt->findNextSenderState<SystolicSenderState>();
      assert(senderState && "Packet did not contain a SystolicSenderState!");
      if (senderState->is_ctrl_signal)
        state = ReadyToWakeupCpu;
    }
    // Currently the systolic array only uses the cache for sending the finish
    // signal. Future use of the cache for storing normal data should be handled
    // here.
  }

  Addr translateAtomic(Addr vaddr, int size) override {
    Addr page_offset = vaddr & pageMask();
    Addr vpn = vaddr & ~pageMask();
    Addr ppn;
    tlb.lookup(vpn, ppn);
    return ppn | page_offset;
  }

  void issueDmaInputRead();
  void issueDmaWeightRead();
  void issueDmaWrite();

  void setDataType(const std::string& type) {
    if (type == "int32") {
      dataType = Int32;
      elemSize = 4;
    } else if (type == "int64") {
      dataType = Int64;
      elemSize = 8;
    } else if (type == "float16") {
      dataType = Float16;
      elemSize = 2;
    } else if (type == "float32") {
      dataType = Float32;
      elemSize = 4;
    } else if (type == "float64") {
      dataType = Float64;
      elemSize = 8;
    } else {
      assert(false && "Unknown data type specified.");
    }
  }

  EventWrapper<SystolicArray, &SystolicArray::processTick> tickEvent;

  // Inifinte TLB memory. We need to use physical address when issuing DMA
  // requests.
  // TODO: Use a realistic TLB model to account for page walk latency when a TLB
  // miss happens.
  InfiniteTLBMemory tlb;

  std::string acceleratorName;
  State state;

  // Command queue for incoming commands from CPUs.
  std::deque<std::unique_ptr<AcceleratorCommand>> commandQueue;

 public:
  // Parameters of the offloaded convolution.
  Addr inputBaseAddr;
  Addr weightBaseAddr;
  Addr outputBaseAddr;
  int inputRows;
  int inputCols;
  int inputChans;
  int weightRows;
  int weightCols;
  int weightChans;
  int outputRows;
  int outputCols;
  int numOfmaps;
  int numKerns;
  int numEffecKerns;
  int stride;
  int inputTopPad;
  int inputBottomPad;
  int inputLeftPad;
  int inputRightPad;
  // If the inputs contain more channels than the weights, start from this one.
  // Otherwise this should always be zero.
  int ifmapStart;
  // If the weights contain more kernels than the results buffer can fit, start
  // from this one. Otherwise this should always be zero.
  int kernStart;
  // True if we want to add the outputs to the data in the output scratchpad.
  // This is used when the weight tensor is tiled channelwise, so we need to
  // accumulate the partial sums across invocations.
  bool accumResults;
  // True if this invocation needs to read inputs/weights.
  bool readInputs;
  bool readWeights;
  // True if this invocation needs to send the results back to the memory using
  // DMA.
  bool sendResults;
  systolic_activation_type actType;
  systolic_activation_params actParams;

  // The outputs/filters are partitioned into folds in order to map to the PE
  // arrays.
  int numOutputFolds;
  int numWeightFolds;

  // Attributes of the systolic array.
  int peArrayRows;
  int peArrayCols;

  // The size of a scratchpad line. A line is the granularity for accessing
  // the scratchpads.
  int lineSize;
  int alignment;
  DataType dataType;
  int elemSize;
  Dataflow* dataflow;
  Scratchpad* inputSpad;
  Scratchpad* weightSpad;
  Scratchpad* outputSpad;

  // Number of systolic array cycles simulated.
  Stats::Scalar numCycles;
};

}  // namespace systolic

#endif  // __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_H__
