#ifndef __SYSTOLIC_ARRAY_SCRATCHPAD_H__
#define __SYSTOLIC_ARRAY_SCRATCHPAD_H__

#include <queue>
#include <utility>

#include "sim/clocked_object.hh"
#include "mem/port.hh"

#include "params/Scratchpad.hh"
#include "debug/SystolicSpad.hh"

namespace systolic {

// This represents the actual data storage of the scratchpad. Note that bank
// conflict is accounted for by counting the number of conflicted requests to
// the same bank in a cycle based on the request address. However, for
// convenience, we don't really store the data in a banked fashion.
class DataChunk {
 public:
  DataChunk(int size) : chunk(size) {}

  void writeData(int index, uint8_t* data, int size) {
    assert(index < chunk.size());
    memcpy(&chunk[index], data, size);
  }

  void readData(int index, uint8_t* data, int size) {
    assert(index < chunk.size());
    memcpy(data, &chunk[index], size);
  }

 protected:
  std::vector<uint8_t> chunk;
};

class Scratchpad : public ClockedObject {
 typedef ScratchpadParams Params;
 public:
  Scratchpad(const Params* p);
  ~Scratchpad() {}

  Port& getPort(const std::string& if_name, PortID idx) override {
    if (if_name == "accelSidePort")
      return accelSidePort;
    else
      fatal("cannot resolve the port name " + if_name);
  }

  void init() override { accelSidePort.sendRangeChange(); }

  void accessData(Addr addr, int size, uint8_t* data, bool isRead);

  void accessData(PacketPtr pkt) {
    Addr addr = pkt->getAddr();
    uint8_t* data = pkt->getPtr<uint8_t>();
    accessData(addr, pkt->getSize(), data, pkt->isRead());
  }

 protected:
  // AccelSidePort is the port closer to the accelerator.
  class AccelSidePort : public SlavePort {
   public:
    AccelSidePort(const std::string& name, Scratchpad* owner)
        : SlavePort(name, owner), spad(owner), stalled(false) {}

    bool sendTimingResp(PacketPtr pkt);

    AddrRangeList getAddrRanges() const override {
      return spad->getAddrRanges();
    }

    bool isStalled() const { return stalled; }

   protected:
    bool recvTimingReq(PacketPtr pkt) override {
      spad->processPacket(pkt);
      return true;
    }

    void recvRespRetry() override;

    Tick recvAtomic(PacketPtr pkt) override { return 0; }
    void recvFunctional(PacketPtr pkt) override {}

    void stallPort() { stalled = true; }
    void unstallPort() { stalled = false; }

    Scratchpad* spad;
    // Here we queue all the responses that were not successfully sent.
    std::queue<PacketPtr> retries;
    bool stalled;
  };

  void processPacket(PacketPtr pkt);

  // Return the bank index for the address based on the used banking mechanism.
  int getBankIndex(Addr addr);

  void scheduleWakeupEvent(Tick when);

  // Wake up to send requests back from the return queue if they have accounted
  // for the access latency, and reprocess the requests that had bank conflicts
  // in the previous cycle.
  void wakeup();

  const AddrRangeList& getAddrRanges() const { return addrRanges; }

  enum PartitionType {
    InvalidPartType,
    Cyclic,
    Block
  };

  // Event used to wake up the scratchpad to send the completed requests back to
  // the accelerator.
  EventWrapper<Scratchpad, &Scratchpad::wakeup> wakeupEvent;

  AccelSidePort accelSidePort;

  // Address range of this memory
  AddrRangeList addrRanges;

  // The actual data store for the scratchpad.
  DataChunk chunk;

  PartitionType partType;

  int lineSize;

  // Number of banks in this scrarchpad.
  int numBanks;

  // Number of ports per bank.
  int numPorts;

  // Number of accesses to every bank at the recorded tick.
  std::pair<Tick, std::vector<int>> numBankAccess;

  // The queue of packets that wait for their access latency to be accounted for
  // and sent back to the accelerator.
  std::queue<std::pair<Tick, PacketPtr>> returnQueue;

  // The queue of packets that wait for available bandwidth to access the data.
  std::queue<std::pair<Tick, PacketPtr>> waitQueue;
};

};  // namespace systolic

#endif
