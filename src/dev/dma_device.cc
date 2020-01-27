/*
 * Copyright (c) 2012, 2015, 2017, 2019 ARM Limited
 * All rights reserved.
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2006 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Ali Saidi
 *          Nathan Binkert
 *          Andreas Hansson
 *          Andreas Sandberg
 */

#include "dev/dma_device.hh"

#include <utility>

#include "base/chunk_generator.hh"
#include "debug/DMA.hh"
#include "debug/Drain.hh"
#include "mem/port_proxy.hh"
#include "sim/clocked_object.hh"
#include "sim/system.hh"

DmaPort::DmaPort(ClockedObject *dev, System *s, unsigned max_req,
                 unsigned _chunkSize, unsigned _numChannels,
                 bool _invalidateOnWrite, uint32_t sid, uint32_t ssid)
    : MasterPort(dev->name() + ".dma", dev), device(dev), sys(s),
      masterId(s->getMasterId(dev)),
      sendEvent([this] { sendDma(); }, dev->name()),
      sendDataAfterInvalidateEvent(
          [this] { sendDataAfterInvalidate(); }, dev->name()),
      pendingCount(0), inRetry(false), maxRequests(max_req),
      chunkSize(_chunkSize), numChannels(_numChannels),
      invalidateOnWrite(_invalidateOnWrite), defaultSid(sid),
      defaultSSid(ssid) {
    numOutstandingRequests = 0;
    currChannel = 0;
    // Empty DMA channel.
    for (unsigned i = 0; i < numChannels; i++)
        transmitList.push_back(std::deque<PacketPtr>());
    DPRINTF(DMA, "Setting up DMA with transaction chunk size %d\n", chunkSize);
}

DmaPort::DmaPort(ClockedObject *dev, System *s, uint32_t sid, uint32_t ssid)
    : DmaPort(
          dev, s, MAX_DMA_REQUEST, sys->cacheLineSize(), 1, false, sid, ssid) {}

void
DmaPort::handleResp(PacketPtr pkt, Tick delay)
{
    // should always see a response with a sender state
    assert(pkt->isResponse());
    numOutstandingRequests --;

    // get the DMA sender state
    DmaReqState *state = dynamic_cast<DmaReqState*>(pkt->senderState);
    assert(state);

    DPRINTF(DMA, "Received response %s for addr: %#x, addr: %#x size: %d nb: %d,"  \
            " tot: %d sched %d outstanding:%u\n",
            pkt->cmdString(), state->addr,
            pkt->getAddr(), pkt->req->getSize(),
            state->numBytes, state->totBytes,
            state->completionEvent ?
            state->completionEvent->scheduled() : 0,
            numOutstandingRequests);

    assert(pendingCount != 0);
    pendingCount--;

    // update the number of bytes received based on the request rather
    // than the packet as the latter could be rounded up to line sizes
    state->numBytes += pkt->req->getSize();
    assert(state->totBytes >= state->numBytes);

    // if we have reached the total number of bytes for this DMA
    // request, then signal the completion and delete the sate
    if (state->totBytes == state->numBytes) {
        if (state->completionEvent) {
            delay += state->delay;
            device->schedule(state->completionEvent, curTick() + delay);
        }
        delete state;
    }

    // delete the packet
    delete pkt;

    // we might be drained at this point, if so signal the drain event
    if (pendingCount == 0)
        signalDrainDone();
}

bool
DmaPort::recvTimingResp(PacketPtr pkt)
{
    // We shouldn't ever get a cacheable block in Modified state
    assert(pkt->req->isUncacheable() ||
           !(pkt->cacheResponding() && !pkt->hasSharers()) ||
           pkt->isInvalidate());

    handleResp(pkt);

    return true;
}

DmaDevice::DmaDevice(const Params *p)
    : PioDevice(p), dmaPort(this, sys, MAX_DMA_REQUEST, sys->cacheLineSize(), 1,
                            false, p->sid, p->ssid) {}

void
DmaDevice::init()
{
    if (!dmaPort.isConnected())
        panic("DMA port of %s not connected to anything!", name());
    PioDevice::init();
}

DrainState
DmaPort::drain()
{
    if (pendingCount == 0) {
        return DrainState::Drained;
    } else {
        DPRINTF(Drain, "DmaPort not drained\n");
        return DrainState::Draining;
    }
}

void
DmaPort::recvReqRetry()
{
    assert(transmitList.size());
    trySendTimingReq();
}

RequestPtr
DmaPort::dmaAction(Packet::Command cmd, Addr addr, int size, Event *event,
                   uint8_t *data, uint32_t sid, uint32_t ssid, Tick delay,
                   Request::Flags flag)
{
    DPRINTF(DMA, "Starting DMA for addr: %#x size: %d sched: %d\n",
            addr, size, event ? event->scheduled() : -1);

    DmaActionReq dmaActionReq = { cmd,   addr, size, event, data,
                                  delay, flag, sid,  ssid };

    // (functionality added for Table Walker statistics)
    // We're only interested in this when there will only be one request.
    // For simplicity, we return the last request, which would also be
    // the only request in that case.
    RequestPtr final_req = NULL;

    MemCmd memcmd(cmd);
    if (invalidateOnWrite && memcmd.isWrite()) {
        // Delay the dmaAction until all invalidation responses are received.
        outstandingRequests.push_back(dmaActionReq);

        // Create and queue a new DmaActionReq to perform an invalidation of the
        // target region and then initiate the delayed dmaAction when
        // invalidation is complete. Make sure we don't send an uncacheable
        // request for a cache invalidation (that would make no sense).
        Request::Flags inv_flag = flag & ~Request::UNCACHEABLE;
        DmaActionReq invalidateReq = { MemCmd::InvalidateReq,
                                       addr,
                                       size,
                                       event,
                                       nullptr,
                                       delay,
                                       inv_flag,
                                       sid,
                                       ssid };
        DmaReqState *reqState =
            new DmaReqState(&sendDataAfterInvalidateEvent, size, addr, delay);
        final_req = queueDmaAction(invalidateReq, reqState);
    } else {
        // Act on this dmaAction immediately.
        DmaReqState* reqState = new DmaReqState(event, size, addr, delay);
        final_req = queueDmaAction(dmaActionReq, reqState);
    }

    // in zero time also initiate the sending of the packets we have
    // just created, for atomic this involves actually completing all
    // the requests
    sendDma();

    return final_req;
}

/* Find the next empty channel.
 *
 * If there are no empty channels, this returns the current channel.
 */
unsigned
DmaPort::findNextEmptyChannel() {
    unsigned nextChannel = currChannel;
    do {
        if (transmitList[nextChannel].empty())
            break;
        nextChannel = (nextChannel + 1) % numChannels;
    } while (nextChannel != currChannel);
    return nextChannel;
}

/* Find the next non-empty channel.
 *
 * If all channels are empty, this return zero.
 */
unsigned
DmaPort::findNextNonEmptyChannel() {
    unsigned nextChannel = (currChannel + 1) % numChannels;
    while (nextChannel != currChannel) {
        if (!transmitList[nextChannel].empty())
            break;
        nextChannel = (nextChannel + 1) % numChannels;
    }

    if (transmitList[nextChannel].empty()) {
        // We didn't find any non-empty channels. Reset channel to zero.
        return 0;
    } else {
        return nextChannel;
    }
}

RequestPtr
DmaPort::dmaAction(Packet::Command cmd, Addr addr, int size, Event *event,
                   uint8_t *data, Tick delay, Request::Flags flag)
{
    return dmaAction(cmd, addr, size, event, data,
                     defaultSid, defaultSSid, delay, flag);
}

void
DmaPort::queueDma(unsigned channel_idx, PacketPtr pkt)
{
    transmitList[channel_idx].push_back(pkt);

    // remember that we have another packet pending, this will only be
    // decremented once a response comes back
    pendingCount++;
}

void
DmaPort::trySendTimingReq()
{
    // send the first packet on the transmit list and schedule the
    // following send if it is successful
    assert(transmitList[currChannel].size());
    PacketPtr pkt = transmitList[currChannel].front();

    DPRINTF(DMA, "Trying to send %s addr %#x of size %d\n", pkt->cmdString(),
            pkt->getAddr(), pkt->req->getSize());

    inRetry = !sendTimingReq(pkt);
    if (!inRetry) {
        // pop the first packet in the current channel
        transmitList[currChannel].pop_front();
        DPRINTF(DMA,
               "Sent %s addr %#x with size %d from channel %d. \n",
                pkt->cmdString(),
                pkt->getAddr(),
                pkt->req->getSize(),
                currChannel);

        currChannel = findNextNonEmptyChannel();
        DPRINTF(DMA, "-- Done\n");
        numOutstandingRequests++;
        // if there is more to do, then do so
        if (!transmitList[currChannel].empty()) {
            // this should ultimately wait for as many cycles as the
            // device needs to send the packet, but currently the port
            // does not have any known width so simply wait a single
            // cycle
            device->schedule(sendEvent, device->clockEdge(Cycles(1)));
        }
    } else {
        DPRINTF(DMA, "-- Failed, waiting for retry\n");
    }

    DPRINTF(DMA, "TransmitList: %d, inRetry: %d\n",
            transmitList.size(), inRetry);
}

void DmaPort::sendDataAfterInvalidate() {
    if (outstandingRequests.empty())
        return;

    DmaActionReq& dmaReq = outstandingRequests.front();
    DmaReqState *reqState =
        new DmaReqState(dmaReq.event, dmaReq.size, dmaReq.addr, dmaReq.delay);
    DPRINTF(DMA, "Sending DMA after invalidation for addr: %#x size: %d\n",
            dmaReq.addr, dmaReq.size);
    queueDmaAction(dmaReq, reqState);
    outstandingRequests.pop_front();
    sendDma();
}

RequestPtr DmaPort::queueDmaAction(DmaActionReq &dmaReq,
                                   DmaReqState *reqState) {
    /* TODO: Currently as we dynamically add channels, the channel ID is the
     * last channel that is just added. If we switch to the fixed-number of
     * channels model, we can let users to pick which channel they want to use,
     * or automatically pick the empty channel. */
    unsigned channel = findNextEmptyChannel();
    RequestPtr req = NULL;
    MemCmd memcmd(dmaReq.cmd);
    for (ChunkGenerator gen(dmaReq.addr, dmaReq.size, sys->cacheLineSize());
         !gen.done(); gen.next()) {
        req = std::make_shared<Request>(
            gen.addr(), gen.size(), dmaReq.flag, masterId);

        req->setStreamId(dmaReq.sid);
        req->setSubStreamId(dmaReq.ssid);

        req->taskId(ContextSwitchTaskId::DMA);
        PacketPtr pkt = new Packet(req, dmaReq.cmd);

        // Increment the data pointer on a write
        if (dmaReq.data)
            pkt->dataStatic(dmaReq.data + gen.complete());

        pkt->senderState = reqState;

        DPRINTF(DMA, "--Queuing %s for addr: %#x size: %d in channel %d\n",
                memcmd.isInvalidate() ? "invalidation" : "DMA", gen.addr(),
                gen.size(), channel);
        queueDma(channel, pkt);
    }
    return req;
}

void
DmaPort::sendDma()
{
    // some kind of selcetion between access methods
    // more work is going to have to be done to make
    // switching actually work
    assert(transmitList.size());

    if (sys->isTimingMode()) {
        // if we are either waiting for a retry or are still waiting
        // after sending the last packet, then do not proceed
        // or number of outstanding requests > max requests
        if (inRetry || sendEvent.scheduled() ) {
            DPRINTF(DMA, "Can't send immediately, waiting to send\n");
            return;
        }
        if (numOutstandingRequests >= maxRequests) {
            device->schedule(sendEvent, device->clockEdge(Cycles(1)));
            DPRINTF(DMA, "Too many outstanding requests, try again next cycle...\n");
            return;
        }

        trySendTimingReq();
    } else if (sys->isAtomicMode()) {
        // send everything there is to send in zero time
        for (auto& it : transmitList) {
          while(!it.empty()){
            PacketPtr pkt = it.front();
            it.pop_front();
            DPRINTF(DMA, "Sending  DMA for addr: %#x size: %d\n",
                    pkt->req->getPaddr(), pkt->req->getSize());
            Tick lat = sendAtomic(pkt);
            numOutstandingRequests++;

            handleResp(pkt, lat);
          }
        }
    } else
        panic("Unknown memory mode.");
}

Addr
DmaPort::getPacketAddr(PacketPtr pkt) {
  DmaReqState *state = pkt->findNextSenderState<DmaReqState>();
  assert(state && "No DmaReqState found!");
  return state->addr;
}

Event*
DmaPort::getPacketCompletionEvent(PacketPtr pkt) {
    DmaReqState *state = pkt->findNextSenderState<DmaReqState>();
    assert(state && "No DmaReqState found!");
    return state->completionEvent;
}

Port &
DmaDevice::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "dma") {
        return dmaPort;
    }
    return PioDevice::getPort(if_name, idx);
}

DmaReadFifo::DmaReadFifo(DmaPort &_port, size_t size,
                         unsigned max_req_size,
                         unsigned max_pending,
                         Request::Flags flags)
    : maxReqSize(max_req_size), fifoSize(size),
      reqFlags(flags), port(_port),
      buffer(size),
      nextAddr(0), endAddr(0)
{
    freeRequests.resize(max_pending);
    for (auto &e : freeRequests)
        e.reset(new DmaDoneEvent(this, max_req_size));

}

DmaReadFifo::~DmaReadFifo()
{
    for (auto &p : pendingRequests) {
        DmaDoneEvent *e(p.release());

        if (e->done()) {
            delete e;
        } else {
            // We can't kill in-flight DMAs, so we'll just transfer
            // ownership to the event queue so that they get freed
            // when they are done.
            e->kill();
        }
    }
}

void
DmaReadFifo::serialize(CheckpointOut &cp) const
{
    assert(pendingRequests.empty());

    SERIALIZE_CONTAINER(buffer);
    SERIALIZE_SCALAR(endAddr);
    SERIALIZE_SCALAR(nextAddr);
}

void
DmaReadFifo::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_CONTAINER(buffer);
    UNSERIALIZE_SCALAR(endAddr);
    UNSERIALIZE_SCALAR(nextAddr);
}

bool
DmaReadFifo::tryGet(uint8_t *dst, size_t len)
{
    if (buffer.size() >= len) {
        buffer.read(dst, len);
        resumeFill();
        return true;
    } else {
        return false;
    }
}

void
DmaReadFifo::get(uint8_t *dst, size_t len)
{
    const bool success(tryGet(dst, len));
    panic_if(!success, "Buffer underrun in DmaReadFifo::get()\n");
}

void
DmaReadFifo::startFill(Addr start, size_t size)
{
    assert(atEndOfBlock());

    nextAddr = start;
    endAddr = start + size;
    resumeFill();
}

void
DmaReadFifo::stopFill()
{
    // Prevent new DMA requests by setting the next address to the end
    // address. Pending requests will still complete.
    nextAddr = endAddr;

    // Flag in-flight accesses as canceled. This prevents their data
    // from being written to the FIFO.
    for (auto &p : pendingRequests)
        p->cancel();
}

void
DmaReadFifo::resumeFill()
{
    // Don't try to fetch more data if we are draining. This ensures
    // that the DMA engine settles down before we checkpoint it.
    if (drainState() == DrainState::Draining)
        return;

    const bool old_eob(atEndOfBlock());

    if (port.sys->bypassCaches())
        resumeFillFunctional();
    else
        resumeFillTiming();

    if (!old_eob && atEndOfBlock())
        onEndOfBlock();
}

void
DmaReadFifo::resumeFillFunctional()
{
    const size_t fifo_space = buffer.capacity() - buffer.size();
    const size_t kvm_watermark = port.sys->cacheLineSize();
    if (fifo_space >= kvm_watermark || buffer.capacity() < kvm_watermark) {
        const size_t block_remaining = endAddr - nextAddr;
        const size_t xfer_size = std::min(fifo_space, block_remaining);
        std::vector<uint8_t> tmp_buffer(xfer_size);

        assert(pendingRequests.empty());
        DPRINTF(DMA, "KVM Bypassing startAddr=%#x xfer_size=%#x " \
                "fifo_space=%#x block_remaining=%#x\n",
                nextAddr, xfer_size, fifo_space, block_remaining);

        port.sys->physProxy.readBlob(nextAddr, tmp_buffer.data(), xfer_size);
        buffer.write(tmp_buffer.begin(), xfer_size);
        nextAddr += xfer_size;
    }
}

void
DmaReadFifo::resumeFillTiming()
{
    size_t size_pending(0);
    for (auto &e : pendingRequests)
        size_pending += e->requestSize();

    while (!freeRequests.empty() && !atEndOfBlock()) {
        const size_t req_size(std::min(maxReqSize, endAddr - nextAddr));
        if (buffer.size() + size_pending + req_size > fifoSize)
            break;

        DmaDoneEventUPtr event(std::move(freeRequests.front()));
        freeRequests.pop_front();
        assert(event);

        event->reset(req_size);
        port.dmaAction(MemCmd::ReadReq, nextAddr, req_size, event.get(),
                       event->data(), 0, reqFlags);
        nextAddr += req_size;
        size_pending += req_size;

        pendingRequests.emplace_back(std::move(event));
    }
}

void
DmaReadFifo::dmaDone()
{
    const bool old_active(isActive());

    handlePending();
    resumeFill();

    if (old_active && !isActive())
        onIdle();
}

void
DmaReadFifo::handlePending()
{
    while (!pendingRequests.empty() && pendingRequests.front()->done()) {
        // Get the first finished pending request
        DmaDoneEventUPtr event(std::move(pendingRequests.front()));
        pendingRequests.pop_front();

        if (!event->canceled())
            buffer.write(event->data(), event->requestSize());

        // Move the event to the list of free requests
        freeRequests.emplace_back(std::move(event));
    }

    if (pendingRequests.empty())
        signalDrainDone();
}

DrainState
DmaReadFifo::drain()
{
    return pendingRequests.empty() ? DrainState::Drained : DrainState::Draining;
}


DmaReadFifo::DmaDoneEvent::DmaDoneEvent(DmaReadFifo *_parent,
                                        size_t max_size)
    : parent(_parent), _done(false), _canceled(false), _data(max_size, 0)
{
}

void
DmaReadFifo::DmaDoneEvent::kill()
{
    parent = nullptr;
    setFlags(AutoDelete);
}

void
DmaReadFifo::DmaDoneEvent::cancel()
{
    _canceled = true;
}

void
DmaReadFifo::DmaDoneEvent::reset(size_t size)
{
    assert(size <= _data.size());
    _done = false;
    _canceled = false;
    _requestSize = size;
}

void
DmaReadFifo::DmaDoneEvent::process()
{
    if (!parent)
        return;

    assert(!_done);
    _done = true;
    parent->dmaDone();
}
