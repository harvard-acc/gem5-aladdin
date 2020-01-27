#ifndef __SYSTOLIC_ARRAY_LOCAL_SPAD_INTERFACE_H__
#define __SYSTOLIC_ARRAY_LOCAL_SPAD_INTERFACE_H__

#include <queue>

#include "params/SystolicArray.hh"
#include "debug/SystolicInterface.hh"

namespace systolic {

class SystolicArray;

// This is the base class for units in the accelerator that directly interact
// with the local scratchpad.
class LocalSpadInterface {
 public:
  LocalSpadInterface(const std::string& name,
                     SystolicArray& accel,
                     const SystolicArrayParams& params);

  Port& getLocalSpadPort() { return localSpadPort; }

  virtual void evaluate() = 0;

 protected:
  // This port is intended to communicate between the local scratchpad interface
  // and the scratchpad.
  class LocalSpadPort : public MasterPort {
   public:
    LocalSpadPort(const std::string& name,
                  Gem5Datapath* dev,
                  LocalSpadInterface& _spadIf);

    bool sendTimingReq(PacketPtr pkt);

    bool isStalled() const { return stalled; }

   protected:
    bool recvTimingResp(PacketPtr pkt) override {
      spadIf.localSpadCallback(pkt);
      return true;
    }

    void recvReqRetry() override;

    void recvTimingSnoopReq(PacketPtr pkt) override {}
    void recvFunctionalSnoop(PacketPtr pkt) override {}
    Tick recvAtomicSnoop(PacketPtr pkt) override { return 0; }

    void stallPort() { stalled = true; }
    void unstallPort() { stalled = false; }

    LocalSpadInterface& spadIf;
    // Here we queue all the requests that were not successfully sent.
    std::queue<PacketPtr> retries;
    bool stalled;
  };

  const std::string& name() { return unitName; }

  // Callback function on receiving response from scratchpad.
  virtual void localSpadCallback(PacketPtr pkt) = 0;

  // Name of this unit.
  const std::string unitName;

  // Port to the local scratchpad and its ID.
  LocalSpadPort localSpadPort;
  MasterID localSpadMasterId;
};

}  // namespace systolic

#endif
