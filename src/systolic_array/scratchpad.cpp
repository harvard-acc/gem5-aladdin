#include "scratchpad.h"

namespace systolic {

Scratchpad::Scratchpad(const Params* p)
    : ClockedObject(p), accelSidePort(name() + ".accel_side_port", this),
      addrRanges(p->addrRanges.begin(), p->addrRanges.end()), chunk(p->size),
      lineSize(p->lineSize), partType(InvalidPartType), numBanks(p->numBanks),
      numPorts(p->numPorts), numBankAccess(0, std::vector<int>(numBanks)),
      wakeupEvent(this) {
  if (p->partType == "cyclic")
    partType = Cyclic;
  else if (p->partType == "block")
    partType = Block;
  else
    assert(false && "Unknown parition type.");
}

void Scratchpad::accessData(Addr addr, int size, uint8_t* data, bool isRead) {
  uint8_t* ptr = nullptr;
  Addr currAddr = addr;
  for (int i = 0; i < size; i += lineSize) {
    ptr = &data[i];
    if (isRead)
      chunk.readData(currAddr, ptr, lineSize);
    else
      chunk.writeData(currAddr, ptr, lineSize);
    currAddr += lineSize;
  }
}

void Scratchpad::processPacket(PacketPtr pkt) {
  DPRINTF(SystolicSpad, "Received request, addr %#x, master id %d.\n",
          pkt->getAddr(), pkt->masterId());
  Tick now = clockEdge();
  Tick& then = numBankAccess.first;
  std::vector<int>& banks = numBankAccess.second;
  assert(then <= now);
  if (then < now) {
    // The bank access status has become stale, update it for the current
    // cycle.
    numBankAccess.first = now;
    std::fill(banks.begin(), banks.end(), 0);
  }

  int bankIndex = getBankIndex(pkt->getAddr());
  if (++banks[bankIndex] > numPorts) {
    // Not enough bandwidth for this request, a bank conflict encountered.
    // Push the request to the wait queue and wake up next cycle to re-process
    // it.
    waitQueue.push({ now + 1, pkt });
    scheduleWakeupEvent(clockEdge(Cycles(1)));
  } else {
    // Push the request to the return queue to account for the data access
    // latency. Assume the SRAM access latency is 1 for now.
    // TODO: make the SRAM access latency configurable.
    returnQueue.push({ now + 1, pkt });
    scheduleWakeupEvent(clockEdge(Cycles(1)));
  }
}

int Scratchpad::getBankIndex(Addr addr) {
  size_t bankIndex = 0;
  if (partType == Cyclic) {
    bankIndex = (addr / lineSize) % numBanks;
  } else if (partType == Block) {
    // TODO: implement the block fashion of banking mechanism.
    assert("Not implemented yet.");
  }
  return bankIndex;
}

void Scratchpad::scheduleWakeupEvent(Tick when) {
  if (when <= clockEdge())
    when = clockEdge(Cycles(1));
  if (wakeupEvent.scheduled()) {
    if (when < wakeupEvent.when()) {
      deschedule(wakeupEvent);
      schedule(wakeupEvent, when);
    }
  } else {
    schedule(wakeupEvent, when);
  }
}

void Scratchpad::wakeup() {
  Tick now = clockEdge();
  // Send back completed packets in the return queue.
  while (!returnQueue.empty() && returnQueue.front().first <= now &&
         !accelSidePort.isStalled()) {
    PacketPtr pkt = returnQueue.front().second;
    pkt->makeResponse();
    // Access the data.
    accessData(pkt);
    returnQueue.pop();
    if (!accelSidePort.sendTimingResp(pkt)) {
      DPRINTF(SystolicSpad,
              "Sending response needs retry, addr %#x, master id %d.\n",
              pkt->getAddr(), pkt->masterId());
      break;
    } else {
      DPRINTF(SystolicSpad, "Response sent, addr %#x, master id %d.\n",
              pkt->getAddr(), pkt->masterId());
    }
  }

  // Re-process the requests that had bank conflicts.
  while (!waitQueue.empty() && returnQueue.front().first <= now) {
    processPacket(waitQueue.front().second);
    waitQueue.pop();
  }

  // Determine the next wakeup time
  if (!returnQueue.empty()) {
    Tick next = returnQueue.front().first;
    scheduleWakeupEvent(next);
  }
}

bool Scratchpad::AccelSidePort::sendTimingResp(PacketPtr pkt) {
  if (isStalled()) {
    assert(!retries.empty() && "Must have retries waiting to be stalled");
    retries.push(pkt);
    DPRINTF(SystolicSpad,
            "Response needs retry due to stalled port, addr %#x.\n",
            pkt->getAddr());
    return false;
  } else if (!SlavePort::sendTimingResp(pkt)) {
    // Need to stall the port until a recvRespRetry() is received, which
    // indicates the bus becomes available.
    stallPort();
    retries.push(pkt);
    DPRINTF(SystolicSpad,
            "Response needs retry due to unavailable bandwidth, addr %#x.\n",
            pkt->getAddr());
    return false;
  } else {
    DPRINTF(SystolicSpad, "Response sent, addr %#x.\n", pkt->getAddr());
    return true;
  }
}

void Scratchpad::AccelSidePort::recvRespRetry() {
  unstallPort();
  while (!retries.empty()) {
    PacketPtr pkt = retries.front();
    if (!SlavePort::sendTimingResp(pkt)) {
      stallPort();
      DPRINTF(SystolicSpad,
              "Response retry sending failed, addr %#x.\n",
              pkt->getAddr());
      break;
    } else {
      DPRINTF(SystolicSpad,
              "Response retry sending successful, addr %#x.\n",
              pkt->getAddr());
      retries.pop();
    }
  }
}

}  // namespace systolic

systolic::Scratchpad* ScratchpadParams::create() {
  return new systolic::Scratchpad(this);
}
