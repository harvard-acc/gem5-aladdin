/*
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
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
 * Authors: Erik Hallnor
 *          Dave Greene
 *          Steve Reinhardt
 */

/**
 * @file
 * Describes a cache based on template policies.
 */

#ifndef __CACHE_HH__
#define __CACHE_HH__

#include "base/misc.hh" // fatal, panic, and warn
#include "cpu/smt.hh" // SMT_MAX_THREADS

#include "mem/cache/base_cache.hh"
#include "mem/cache/prefetch/prefetcher.hh"

// forward declarations
class Bus;

/**
 * A template-policy based cache. The behavior of the cache can be altered by
 * supplying different template policies. TagStore handles all tag and data
 * storage @sa TagStore. Buffering handles all misses and writes/writebacks
 * @sa MissQueue. Coherence handles all coherence policy details @sa
 * UniCoherence, SimpleMultiCoherence.
 */
template <class TagStore, class Buffering, class Coherence>
class Cache : public BaseCache
{
  public:
    /** Define the type of cache block to use. */
    typedef typename TagStore::BlkType BlkType;

    bool prefetchAccess;
  protected:

    /** Tag and data Storage */
    TagStore *tags;
    /** Miss and Writeback handler */
    Buffering *missQueue;
    /** Coherence protocol. */
    Coherence *coherence;

    /** Prefetcher */
    Prefetcher<TagStore, Buffering> *prefetcher;

    /** Do fast copies in this cache. */
    bool doCopy;

    /** Block on a delayed copy. */
    bool blockOnCopy;

    /**
     * The clock ratio of the outgoing bus.
     * Used for calculating critical word first.
     */
    int busRatio;

     /**
      * The bus width in bytes of the outgoing bus.
      * Used for calculating critical word first.
      */
    int busWidth;

     /**
      * A permanent mem req to always be used to cause invalidations.
      * Used to append to target list, to cause an invalidation.
      */
    Packet * invalidatePkt;

    /**
     * Temporarily move a block into a MSHR.
     * @todo Remove this when LSQ/SB are fixed and implemented in memtest.
     */
    void pseudoFill(Addr addr, int asid);

    /**
     * Temporarily move a block into an existing MSHR.
     * @todo Remove this when LSQ/SB are fixed and implemented in memtest.
     */
    void pseudoFill(MSHR *mshr);

  public:

    class Params
    {
      public:
        TagStore *tags;
        Buffering *missQueue;
        Coherence *coherence;
        bool doCopy;
        bool blockOnCopy;
        BaseCache::Params baseParams;
        Bus *in;
        Bus *out;
        Prefetcher<TagStore, Buffering> *prefetcher;
        bool prefetchAccess;

        Params(TagStore *_tags, Buffering *mq, Coherence *coh,
               bool do_copy, BaseCache::Params params, Bus * in_bus,
               Bus * out_bus, Prefetcher<TagStore, Buffering> *_prefetcher,
               bool prefetch_access)
            : tags(_tags), missQueue(mq), coherence(coh), doCopy(do_copy),
              blockOnCopy(false), baseParams(params), in(in_bus), out(out_bus),
              prefetcher(_prefetcher), prefetchAccess(prefetch_access)
        {
        }
    };

    /** Instantiates a basic cache object. */
    Cache(const std::string &_name, HierParams *hier_params, Params &params);

    void regStats();

    /**
     * Performs the access specified by the request.
     * @param req The request to perform.
     * @return The result of the access.
     */
    MemAccessResult access(Packet * &pkt);

    /**
     * Selects a request to send on the bus.
     * @return The memory request to service.
     */
    Packet * getPacket();

    /**
     * Was the request was sent successfully?
     * @param req The request.
     * @param success True if the request was sent successfully.
     */
    void sendResult(Packet * &pkt, bool success);

    /**
     * Handles a response (cache line fill/write ack) from the bus.
     * @param req The request being responded to.
     */
    void handleResponse(Packet * &pkt);

    /**
     * Start handling a copy transaction.
     * @param req The copy request to perform.
     */
    void startCopy(Packet * &pkt);

    /**
     * Handle a delayed copy transaction.
     * @param req The delayed copy request to continue.
     * @param addr The address being responded to.
     * @param blk The block of the current response.
     * @param mshr The mshr being handled.
     */
    void handleCopy(Packet * &pkt, Addr addr, BlkType *blk, MSHR *mshr);

    /**
     * Selects a coherence message to forward to lower levels of the hierarchy.
     * @return The coherence message to forward.
     */
    Packet * getCoherenceReq();

    /**
     * Snoops bus transactions to maintain coherence.
     * @param req The current bus transaction.
     */
    void snoop(Packet * &pkt);

    void snoopResponse(Packet * &pkt);

    /**
     * Invalidates the block containing address if found.
     * @param addr The address to look for.
     * @param asid The address space ID of the address.
     * @todo Is this function necessary?
     */
    void invalidateBlk(Addr addr, int asid);

    /**
     * Aquash all requests associated with specified thread.
     * intended for use by I-cache.
     * @param req->getThreadNum()ber The thread to squash.
     */
    void squash(int threadNum)
    {
        missQueue->squash(threadNum);
    }

    /**
     * Return the number of outstanding misses in a Cache.
     * Default returns 0.
     *
     * @retval unsigned The number of missing still outstanding.
     */
    unsigned outstandingMisses() const
    {
        return missQueue->getMisses();
    }

    /**
     * Send a response to the slave interface.
     * @param req The request being responded to.
     * @param time The time the response is ready.
     */
    void respond(Packet * &pkt, Tick time)
    {
        si->respond(pkt,time);
    }

    /**
     * Perform the access specified in the request and return the estimated
     * time of completion. This function can either update the hierarchy state
     * or just perform the access wherever the data is found depending on the
     * state of the update flag.
     * @param req The memory request to satisfy
     * @param update If true, update the hierarchy, otherwise just perform the
     * request.
     * @return The estimated completion time.
     */
    Tick probe(Packet * &pkt, bool update);

    /**
     * Snoop for the provided request in the cache and return the estimated
     * time of completion.
     * @todo Can a snoop probe not change state?
     * @param req The memory request to satisfy
     * @param update If true, update the hierarchy, otherwise just perform the
     * request.
     * @return The estimated completion time.
     */
    Tick snoopProbe(Packet * &pkt, bool update);
};

#endif // __CACHE_HH__
