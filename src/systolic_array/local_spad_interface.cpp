#include "systolic_array.h"
#include "local_spad_interface.h"

namespace systolic {

LocalSpadInterface::LocalSpadInterface(const std::string& name,
                                       SystolicArray& accel,
                                       const SystolicArrayParams& params)
    : localSpadPort(name, &accel, *this), unitName(name),
      localSpadMasterId(
          params.system->getMasterId(&accel, name + ".local_spad")) {}

LocalSpadInterface::LocalSpadPort::LocalSpadPort(const std::string& name,
                                                 Gem5Datapath* dev,
                                                 LocalSpadInterface& _spadIf)
    : MasterPort(name, dev), spadIf(_spadIf), stalled(false) {}

bool LocalSpadInterface::LocalSpadPort::sendTimingReq(PacketPtr pkt) {
  if (isStalled()) {
    assert(!retries.empty() && "Must have retries waiting to be stalled");
    retries.push(pkt);
    DPRINTF(SystolicInterface,
            "Request needs retry due to stalled port, addr %#x.\n",
            pkt->getAddr());
    return false;
  } else if (!MasterPort::sendTimingReq(pkt)) {
    // Need to stall the port until a recvReqRetry() is received, which
    // indicates the bus becomes available.
    stallPort();
    retries.push(pkt);
    DPRINTF(SystolicInterface,
            "Request needs retry due to unavailable bandwidth, addr %#x.\n",
            pkt->getAddr());
    return false;
  } else {
    DPRINTF(SystolicInterface, "Request sent, addr %#x.\n", pkt->getAddr());
    return true;
  }
}

void LocalSpadInterface::LocalSpadPort::recvReqRetry() {
  unstallPort();
  while (!retries.empty()) {
    PacketPtr pkt = retries.front();
    if (!MasterPort::sendTimingReq(pkt)) {
      stallPort();
      DPRINTF(SystolicInterface,
              "Request retry sending failed, addr %#x.\n",
              pkt->getAddr());
      break;
    } else {
      DPRINTF(SystolicInterface,
              "Request retry sending successful, addr %#x.\n",
              pkt->getAddr());
      retries.pop();
    }
  }
}

}  // namespace systolic
