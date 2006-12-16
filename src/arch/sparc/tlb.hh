/*
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
 */

#ifndef __ARCH_SPARC_TLB_HH__
#define __ARCH_SPARC_TLB_HH__

#include "arch/sparc/tlb_map.hh"
#include "base/misc.hh"
#include "mem/request.hh"
#include "sim/faults.hh"
#include "sim/sim_object.hh"

class ThreadContext;
class Packet;

namespace SparcISA
{

class TLB : public SimObject
{
  protected:
    TlbMap lookupTable;;
    typedef TlbMap::iterator MapIter;

    TlbEntry *tlb;

    int size;
    int usedEntries;

    enum FaultTypes {
        OtherFault = 0,
        PrivViolation = 0x1,
        SideEffect = 0x2,
        AtomicToIo = 0x4,
        IllegalAsi = 0x8,
        LoadFromNfo = 0x10,
        VaOutOfRange = 0x20,
        VaOutOfRangeJmp = 0x40
    };

    enum ContextType {
        Primary = 0,
        Secondary = 1,
        Nucleus = 2
    };


    /** lookup an entry in the TLB based on the partition id, and real bit if
     * real is true or the partition id, and context id if real is false.
     * @param va the virtual address not shifted (e.g. bottom 13 bits are 0)
     * @param paritition_id partition this entry is for
     * @param real is this a real->phys or virt->phys translation
     * @param context_id if this is virt->phys what context
     * @return A pointer to a tlb entry
     */
    TlbEntry *lookup(Addr va, int partition_id, bool real, int context_id = 0);

    /** Insert a PTE into the TLB. */
    void insert(Addr vpn, int partition_id, int context_id, bool real,
            const PageTableEntry& PTE, int entry = -1);

    /** Given an entry id, read that tlb entries' tag. */
    uint64_t TagRead(int entry);

    /** Give an entry id, read that tlb entries' tte */
    uint64_t TteRead(int entry);

    /** Remove all entries from the TLB */
    void invalidateAll();

    /** Remove all non-locked entries from the tlb that match partition id. */
    void demapAll(int partition_id);

    /** Remove all entries that match a given context/partition id. */
    void demapContext(int partition_id, int context_id);

    /** Remve all entries that match a certain partition id, (contextid), and
     * va). */
    void demapPage(Addr va, int partition_id, bool real, int context_id);

    /** Checks if the virtual address provided is a valid one. */
    bool validVirtualAddress(Addr va, bool am);

    void writeSfsr(ThreadContext *tc, int reg, bool write, ContextType ct,
            bool se, FaultTypes ft, int asi);

    void clearUsedBits();


    void writeTagAccess(ThreadContext *tc, int reg, Addr va, int context);

  public:
    TLB(const std::string &name, int size);

    void dumpAll();

    // Checkpointing
    virtual void serialize(std::ostream &os);
    virtual void unserialize(Checkpoint *cp, const std::string &section);
};

class ITB : public TLB
{
  public:
    ITB(const std::string &name, int size) : TLB(name, size)
    {
    }

    Fault translate(RequestPtr &req, ThreadContext *tc);
  private:
    void writeSfsr(ThreadContext *tc, bool write, ContextType ct,
            bool se, FaultTypes ft, int asi);
    void writeTagAccess(ThreadContext *tc, Addr va, int context);
    friend class DTB;
};

class DTB : public TLB
{
  public:
    DTB(const std::string &name, int size) : TLB(name, size)
    {
    }

    Fault translate(RequestPtr &req, ThreadContext *tc, bool write);
    Tick doMmuRegRead(ThreadContext *tc, Packet *pkt);
    Tick doMmuRegWrite(ThreadContext *tc, Packet *pkt);

  private:
    void writeSfr(ThreadContext *tc, Addr a, bool write, ContextType ct,
            bool se, FaultTypes ft, int asi);
    void writeTagAccess(ThreadContext *tc, Addr va, int context);


};

}

#endif // __ARCH_SPARC_TLB_HH__
