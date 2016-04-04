/*
 * Copyright (c) 2012 ARM Limited
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
 */

#include "base/chunk_generator.hh"
#include "debug/DMA.hh"
#include "debug/Drain.hh"
#include "dev/dma_device.hh"
#include "sim/system.hh"

DmaPort::DmaPort(MemObject *dev, System *s, unsigned max_req,
                 unsigned _ChunkSize, bool multi_channel)
    : MasterPort(dev->name() + ".dma", dev), device(dev), sendEvent(this),
      sys(s), masterId(s->getMasterId(dev->name())),
      pendingCount(0), drainManager(NULL),
      inRetry(false), maxRequests(max_req),
      ChunkSize(_ChunkSize),
      multi_channel(multi_channel)
{
  numOfOutstandingRequests = 0;
  curr_channel_idx = 0;
  DPRINTF(DMA, "Setting up DMA with transaction chunk size %d\n", ChunkSize);
}

DmaPort::DmaPort(MemObject *dev, System *s, unsigned max_req)
    : DmaPort(dev, s, max_req, sys->cacheLineSize(), false) {}

void
DmaPort::handleResp(PacketPtr pkt, Tick delay)
{
    // should always see a response with a sender state
    assert(pkt->isResponse());
    numOfOutstandingRequests --;

    // get the DMA sender state
    DmaReqState *state = dynamic_cast<DmaReqState*>(pkt->senderState);
    assert(state);

    DPRINTF(DMA, "Received response %s for base addr: %#x, addr: %#x size: %d nb: %d,"  \
            " tot: %d sched %d outstanding:%u\n",
            pkt->cmdString(), state->baseAddr,
            pkt->getAddr(), pkt->req->getSize(),
            state->numBytes, state->totBytes,
            state->completionEvent ?
            state->completionEvent->scheduled() : 0,
            numOfOutstandingRequests);

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

    // delete the request that we created and also the packet
    delete pkt->req;
    delete pkt;

    // we might be drained at this point, if so signal the drain event
    if (pendingCount == 0 && drainManager) {
        drainManager->signalDrainDone();
        drainManager = NULL;
    }
}

bool
DmaPort::recvTimingResp(PacketPtr pkt)
{
    // We shouldn't ever get a cacheable block in ownership state
    assert(pkt->req->isUncacheable() ||
           !(pkt->memInhibitAsserted() && !pkt->sharedAsserted()));

    handleResp(pkt);

    return true;
}

DmaDevice::DmaDevice(const Params *p)
    : PioDevice(p), dmaPort(this, sys, MAX_DMA_REQUEST) //Modification for DMA w/ Aladdin
{ }

void
DmaDevice::init()
{
    if (!dmaPort.isConnected())
        panic("DMA port of %s not connected to anything!", name());
    PioDevice::init();
}

unsigned int
DmaDevice::drain(DrainManager *dm)
{
    unsigned int count = pioPort.drain(dm) + dmaPort.drain(dm);
    if (count)
        setDrainState(Drainable::Draining);
    else
        setDrainState(Drainable::Drained);
    return count;
}

unsigned int
DmaPort::drain(DrainManager *dm)
{
    if (pendingCount == 0)
        return 0;
    drainManager = dm;
    DPRINTF(Drain, "DmaPort not drained\n");
    return 1;
}

void
DmaPort::recvReqRetry()
{
    assert(transmitList.size());
    trySendTimingReq();
}

RequestPtr
DmaPort::dmaAction(Packet::Command cmd, Addr base_addr, int offset, int size,
                   Event *event, uint8_t *data, Tick delay, Request::Flags flag)
{
    // one DMA request sender state for every action, that is then
    // split into many requests and packets based on the block size,
    // i.e. cache line size
    DmaReqState *reqState = new DmaReqState(event, size, base_addr, offset, delay);

    // (functionality added for Table Walker statistics)
    // We're only interested in this when there will only be one request.
    // For simplicity, we return the last request, which would also be
    // the only request in that case.
    RequestPtr req = NULL;

    DPRINTF(DMA, "Starting DMA for addr: %#x size: %d sched: %d\n",
            base_addr + offset, size, event ? event->scheduled() : -1);

    /* TODO: Ideally the number of DMA channels should be a fixed hardware
     * constraint, instead of growing up and down at runtime. A better way to do
     * this is to allocate a fixed-number of channels when we initialize the DMA
     * device. We will swtich to that after MICRO. */
    transmitList.push_back(std::deque<PacketPtr>());
    assert (transmitList.size() < MAX_CHANNELS);
    /* TODO: Currently as we dynamically add channels, the channel ID is the
     * last channel that is just added. If we switch to the fixed-number of
     * channels model, we can let users to pick which channel they want to use,
     * or automatically pick the empty channel. */
    unsigned channel_idx = transmitList.size() - 1;
    for (ChunkGenerator gen(base_addr + offset, size, ChunkSize);
         !gen.done(); gen.next()) {
        req = new Request(gen.addr(), gen.size(), flag, masterId);
        req->taskId(ContextSwitchTaskId::DMA);
        PacketPtr pkt = new Packet(req, cmd);

        // Increment the data pointer on a write
        if (data)
            pkt->dataStatic(data + gen.complete());

        pkt->senderState = reqState;

        DPRINTF(DMA, "--Queuing DMA for addr: %#x size: %d in channel %d\n", gen.addr(),
                gen.size(), channel_idx);
        queueDma(channel_idx, pkt);
    }

    // in zero time also initiate the sending of the packets we have
    // just created, for atomic this involves actually completing all
    // the requests
    sendDma();

    return req;
}

RequestPtr
DmaPort::dmaAction(Packet::Command cmd, Addr addr, int size, Event *event,
                   uint8_t *data, Tick delay, Request::Flags flag)
{
    return dmaAction(cmd, addr, 0, size, event, data, delay, flag);
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
    assert(transmitList[curr_channel_idx].size());
    PacketPtr pkt = transmitList[curr_channel_idx].front();

    DPRINTF(DMA, "Trying to send %s addr %#x of size %d\n", pkt->cmdString(),
            pkt->getAddr(), pkt->req->getSize());

    inRetry = !sendTimingReq(pkt);
    if (!inRetry) {
        // pop the first packet in the current channel
        transmitList[curr_channel_idx].pop_front();
        DPRINTF(DMA,
               "Sent %s addr %#x with size %d from channel %d. \n",
                pkt->cmdString(),
                pkt->getAddr(),
                pkt->req->getSize());
        // Find next_channel_id
        int next_channel_idx;
        unsigned num_channels = transmitList.size();
        // If the current channel is empty, or DMA is in multi-channel mode,
        // move on to the next channel.
        if (transmitList[curr_channel_idx].empty() || multi_channel) {
          next_channel_idx = (curr_channel_idx + 1) % num_channels;
          // Check whether next channel is empty
          while (transmitList[next_channel_idx].empty()) {
            if (next_channel_idx == curr_channel_idx) {
              // no more packet after this one.
              next_channel_idx = -1;
              break;
            } else {
              next_channel_idx = (next_channel_idx + 1) % num_channels;
            }
          }
        } else {
          // DMA is not interleaving, and there are more packets to send in curr
          // channel.
          next_channel_idx = curr_channel_idx;
        }
        // assign channel id.
        curr_channel_idx = next_channel_idx;
        DPRINTF(DMA, "-- Done\n");
        numOfOutstandingRequests ++;
        // if there is more to do, then do so
        if (curr_channel_idx != -1) {
            // this should ultimately wait for as many cycles as the
            // device needs to send the packet, but currently the port
            // does not have any known width so simply wait a single
            // cycle
            device->schedule(sendEvent, device->clockEdge(Cycles(1)));
        } else {
          // Reset the channel idx for the next transfer.
          curr_channel_idx = 0;
          transmitList.clear();
        }
    } else {
        DPRINTF(DMA, "-- Failed, waiting for retry\n");
    }

    DPRINTF(DMA, "TransmitList: %d, inRetry: %d\n",
            transmitList.size(), inRetry);
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
        if (numOfOutstandingRequests >= maxRequests) {
            device->schedule(sendEvent, device->clockEdge(Cycles(1)));
            DPRINTF(DMA, "Too many outstanding requests, try again next cycle...\n");
            return;
        }

        trySendTimingReq();
    } else if (sys->isAtomicMode()) {
        // send everything there is to send in zero time
        for (auto it : transmitList) {
          while(!it.empty()){
            PacketPtr pkt = it.front();
            it.pop_front();
            DPRINTF(DMA, "Sending  DMA for addr: %#x size: %d\n",
                    pkt->req->getPaddr(), pkt->req->getSize());
            Tick lat = sendAtomic(pkt);

            handleResp(pkt, lat);
          }
        }
    } else
        panic("Unknown memory mode.");
}

Addr
DmaPort::getPacketBaseAddr(PacketPtr pkt) {
    DmaReqState *state = dynamic_cast<DmaReqState*>(pkt->senderState);
    return state->baseAddr;
}

size_t
DmaPort::getPacketOffset(PacketPtr pkt) {
    DmaReqState *state = dynamic_cast<DmaReqState*>(pkt->senderState);
    return state->offset;
}

BaseMasterPort &
DmaDevice::getMasterPort(const std::string &if_name, PortID idx)
{
    if (if_name == "dma") {
        return dmaPort;
    }
    return PioDevice::getMasterPort(if_name, idx);
}
