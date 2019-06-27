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
#include "base/chunk_generator.hh"
#include "mem/mem_object.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/system.hh"
#include "sim/eventq.hh"
#include "sim/clocked_object.hh"
#include "dev/dma_device.hh"
#include "debug/SystolicArray.hh"
#include "debug/SystolicArrayVerbose.hh"
#include "aladdin/gem5/aladdin_tlb.hh"
#include "aladdin/gem5/Gem5Datapath.h"

#include "systolic_array_datatypes.h"
#include "params/SystolicArray.hh"

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
// Output    InputWindow1 --->  |--PE10--|--PE11--|--PE12--|--PE13--|
// fold     InputWindow2 ---->  |--PE20--|--PE21--|--PE22--|--PE23--|
//   V     InputWindow3 ----->  |--PE30--|--PE31--|--PE32--|--PE33--|
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
//
// TODO: currently we don't have an SRAM model though we do generate SRAM
// read/write traces. Every SRAM access takes 1 cycle. Padding is not
// implemented yet, for now the model only supports valid padding.

class SystolicArray : public Gem5Datapath {
 public:
  typedef SystolicArrayParams Params;
  SystolicArray(const Params* p)
      : Gem5Datapath(p, p->acceleratorId, false, p->system), tickEvent(this),
        acceleratorName(p->acceleratorName), accelStatus(Idle),
        spadPort(this,
                 p->system,
                 p->maxDmaRequests,
                 p->dmaChunkSize,
                 p->numDmaChannels,
                 p->invalidateOnDmaStore),
        spadMasterId(p->system->getMasterId(this, name() + ".spad")),
        cacheMasterId(p->system->getMasterId(this, name() + ".cache")),
        cachePort(this, "cache_port"), outstandingDmaSize(0),
        peArrayRows(p->peArrayRows), peArrayCols(p->peArrayCols),
        sramSize(p->sramSize) {
    system->registerAccelerator(accelerator_id, this);
  }

  ~SystolicArray() { system->deregisterAccelerator(accelerator_id); }

  virtual MasterPort& getSpadPort() { return spadPort; };
  virtual MasterPort& getCachePort() { return cachePort; };
  MasterID getSpadMasterId() { return spadMasterId; };
  MasterID getCacheMasterId() { return cacheMasterId; };

  virtual BaseMasterPort& getMasterPort(const std::string& if_name,
                                        PortID idx = InvalidPortID) {
    if (if_name == "spad_port")
      return getSpadPort();
    else if (if_name == "cache_port")
      return getCachePort();
    else
      return MemObject::getMasterPort(if_name);
  }

  // Returns the tick event that will schedule the next step.
  virtual Event& getTickEvent() { return tickEvent; }

  virtual void setParams(void* accel_params) {
    systolic_array_data_t* accelParams =
        reinterpret_cast<systolic_array_data_t*>(accel_params);

    inputBaseAddr = (Addr)accelParams->input_base_addr;
    weightBaseAddr = (Addr)accelParams->weight_base_addr;
    outputBaseAddr = (Addr)accelParams->output_base_addr;
    inputRows = accelParams->input_dims[1];
    inputCols = accelParams->input_dims[2];
    weightRows = accelParams->weight_dims[1];
    weightCols = accelParams->weight_dims[2];
    outputRows = accelParams->output_dims[1];
    outputCols = accelParams->output_dims[2];
    channels = accelParams->input_dims[3];
    numOfmaps = accelParams->weight_dims[0];
    stride = accelParams->stride;

    // Infer the numbers of folds needed to map the convolution to the PE array.
    numOutputFolds = ceil(outputRows * outputCols * 1.0 / peArrayRows);
    numWeightFolds = ceil(numOfmaps * 1.0 / peArrayCols);
  }

  virtual void initializeDatapath(int delay) {
    assert(accelStatus == Idle &&
           "The systolic array accelerator is not idle!");
    accelStatus = ReadyForDmaRead;
    // Start running the accelerator.
    scheduleOnEventQueue(delay);
  }

  virtual void sendFinishedSignal() {
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

    if (!cachePort.sendTimingReq(pkt)) {
      assert(!cachePort.inRetry());
      cachePort.setRetryPkt(pkt);
      DPRINTF(SystolicArray, "Sending finished signal failed, retrying.\n");
    } else {
      DPRINTF(SystolicArray, "Sent finished signal.\n");
    }
  }

  virtual void insertTLBEntry(Addr vaddr, Addr paddr) {
    DPRINTF(SystolicArray, "Mapping vaddr 0x%x -> paddr 0x%x.\n", vaddr, paddr);
    Addr vpn = vaddr & ~(pageMask());
    Addr ppn = paddr & ~(pageMask());
    DPRINTF(
        SystolicArray, "Inserting TLB entry vpn 0x%x -> ppn 0x%x.\n", vpn, ppn);
    tlb.insert(vpn, ppn);
  }

  virtual void insertArrayLabelToVirtual(const std::string& array_label,
                                         Addr vaddr,
                                         size_t size) {}

  virtual Addr getBaseAddress(std::string label) {
    assert(false && "Should not call this for the systolic array accelerator!");
  }

  virtual void resetTrace() {
    assert(false && "Should not call this for the systolic array accelerator!");
  }

  // Run the systolic array and return the latency.
  int run() {
    DPRINTF(SystolicArray, "Computation starts.\n");
    int lastReadCycle = genSramReads();
    int lastWriteCycle = genSramWrites();

    DPRINTF(SystolicArray,
            "Computation completed. Cycles: %d.\n",
            std::max(lastReadCycle, lastWriteCycle));
    return std::max(lastReadCycle, lastWriteCycle);
  }

  void processTick() {
    if (accelStatus == ReadyForDmaRead) {
      issueDmaRead();
      accelStatus = WaitingForDmaRead;
    } else if (accelStatus == ReadyToCompute) {
      int latency = run();
      schedule(tickEvent, clockEdge(Cycles(latency)));
      accelStatus = ReadyForDmaWrite;
    } else if (accelStatus == ReadyForDmaWrite) {
      issueDmaWrite();
      accelStatus = WaitingForDmaWrite;
    } else if (accelStatus == ReadyToSendFinish) {
      sendFinishedSignal();
      accelStatus = Idle;
    }
    // If the accelerator is still busy, schedule the next tick.
    if (accelStatus != Idle && !tickEvent.scheduled())
      schedule(tickEvent, clockEdge(Cycles(1)));
  }

 private:
  class SpadPort : public DmaPort {
   public:
    SpadPort(SystolicArray* dev,
             System* s,
             unsigned _max_req,
             unsigned _chunk_size,
             unsigned _numChannels,
             bool _invalidateOnDmaStore)
        : DmaPort(dev,
                  s,
                  _max_req,
                  _chunk_size,
                  _numChannels,
                  _invalidateOnDmaStore),
          max_req(_max_req), dev(dev) {}
    // Maximum DMA requests that can be queued.
    const unsigned max_req;

   protected:
    virtual bool recvTimingResp(PacketPtr pkt);
    virtual void recvTimingSnoopReq(PacketPtr pkt) {}
    virtual void recvFunctionalSnoop(PacketPtr pkt) {}
    virtual bool isSnooping() const { return true; }
    SystolicArray* dev;
  };

  // Cache port to send finish signal
  class CachePort : public MasterPort {
   public:
    CachePort(SystolicArray* _systolicAcc, const std::string& _name)
        : MasterPort(_name, _systolicAcc), systolicAcc(_systolicAcc),
          retryPkt(nullptr) {}

    bool inRetry() const { return retryPkt != NULL; }
    void setRetryPkt(PacketPtr pkt) { retryPkt = pkt; }
    void clearRetryPkt() { retryPkt = NULL; }

   protected:
    virtual bool recvTimingResp(PacketPtr pkt);
    virtual void recvTimingSnoopReq(PacketPtr pkt) {}
    virtual void recvFunctionalSnoop(PacketPtr pkt) {}
    virtual Tick recvAtomicSnoop(PacketPtr pkt) { return 0; }
    virtual void recvReqRetry();
    virtual void recvRespRetry() {}
    virtual bool isSnooping() const { return false; }

    SystolicArray* systolicAcc;
    PacketPtr retryPkt;
  };

  enum AccelStatus {
    Idle,
    ReadyForDmaRead,
    WaitingForDmaRead,
    ReadyToCompute,
    ReadyForDmaWrite,
    WaitingForDmaWrite,
    ReadyToSendFinish,
  };

  Addr pageMask() { return system->getPageBytes() - 1; }
  int pageBytes() { return system->getPageBytes(); }

  // Functions used for issuing DMA requests.
  void translate(Addr vaddr, Addr& paddr) {
    Addr page_offset = vaddr & pageMask();
    Addr vpn = vaddr & ~pageMask();
    Addr ppn;
    tlb.lookup(vpn, ppn);
    paddr = ppn | page_offset;
  }
  void sendDmaRequest(Addr startAddr, int size, bool isRead);
  void issueDmaRead();
  void issueDmaWrite();

  // These two function generate SRAM read/write traces and return the cycle
  // when the last SRAM read/write finish.
  int genSramReads();
  int genSramWrites();

  EventWrapper<SystolicArray, &SystolicArray::processTick> tickEvent;

  // DMA port to the scratchpad.
  SpadPort spadPort;
  MasterID spadMasterId;

  // Coherent port to the cache.
  CachePort cachePort;
  MasterID cacheMasterId;

  // Inifinte TLB memory. We need to use physical address when issuing DMA
  // requests.
  // TODO: Use a realistic TLB model to account for page walk latency when a TLB
  // miss happens.
  InfiniteTLBMemory tlb;

  // These track the uncompleted DMA read/write sizes.
  int outstandingDmaSize;

  std::string acceleratorName;
  AccelStatus accelStatus;

  // Parameters of the offloaded convolution.
  int inputBaseAddr;
  int weightBaseAddr;
  int outputBaseAddr;
  int inputRows;
  int inputCols;
  int weightRows;
  int weightCols;
  int outputRows;
  int outputCols;
  int channels;
  int numOfmaps;
  int stride;

  // The outputs/filters are partitioned into folds in order to map to the PE
  // arrays.
  int numOutputFolds;
  int numWeightFolds;

  // Attributes of the systolic array.
  int peArrayRows;
  int peArrayCols;
  int sramSize;
};

#endif  // __SYSTOLIC_ARRAY_SYSTOLIC_ARRAY_H__
