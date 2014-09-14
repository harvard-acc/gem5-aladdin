#ifndef ALADDIN_TLB_HH
#define ALADDIN_TLB_HH

#include <map>
#include <set>
#include <deque>
#include <unordered_map>

#include "base/statistics.hh"
//#include "params/AladdinTLB.hh"
#include "mem/mem_object.hh"
#include "mem/request.hh"

class Datapath;

class AladdinTLBEntry
{
  public: 
    Addr vpn;
    Addr ppn;
    bool free;
    Tick mruTick;
    uint32_t hits;

    AladdinTLBEntry() : vpn(0), ppn(0), free(true), mruTick(0), hits(0){}
    void setMRU() {mruTick = curTick();}
};

class BaseTLBMemory {

public:
    virtual ~BaseTLBMemory(){}
    virtual bool lookup(Addr vpn, Addr& ppn, bool set_mru=true) = 0;
    virtual void insert(Addr vpn, Addr ppn) = 0;
};

class TLBMemory : public BaseTLBMemory {
    int numEntries;
    int sets;
    int ways;
    int pageBytes;

    AladdinTLBEntry **entries;

protected:
    TLBMemory() {}

public:
    TLBMemory(int _numEntries, int associativity, int _pageBytes) :
        numEntries(_numEntries), sets(associativity), pageBytes(_pageBytes)
    {
        if (sets == 0) {
            sets = numEntries;
        }
        assert(numEntries % sets == 0);
        ways = numEntries/sets;
        entries = new AladdinTLBEntry*[ways];
        for (int i=0; i < ways; i++) {
            entries[i] = new AladdinTLBEntry[sets];
        }
    }
    ~TLBMemory()
    {
        for (int i=0; i < sets; i++) {
            delete[] entries[i];
        }
        delete[] entries;
    }

    virtual bool lookup(Addr vpn, Addr& ppn, bool set_mru=true);
    virtual void insert(Addr vpn, Addr ppn);
};

class InfiniteTLBMemory : public BaseTLBMemory {
    std::map<Addr, Addr> entries;
public:
    InfiniteTLBMemory() {}
    ~InfiniteTLBMemory() {}

    bool lookup(Addr vpn, Addr& ppn, bool set_mru=true)
    {
        auto it = entries.find(vpn);
        if (it != entries.end()) {
            ppn = it->second;
            return true;
        } else {
            ppn = Addr(0);
            return false;
        }
    }
    void insert(Addr vpn, Addr ppn)
    {
        entries[vpn] = ppn;
    }
};

class AladdinTLB
{
  private:
    Datapath *datapath;
    
    unsigned numEntries;
    unsigned assoc;
    Cycles hitLatency;
    Cycles missLatency;
    Addr pageBytes;
    
    BaseTLBMemory *tlbMemory;
    

    class deHitQueueEvent : public Event 
    {
      public: 
        /*Constructs a deHitQueueEvent*/
        deHitQueueEvent(AladdinTLB *_tlb);
        /*Processes the event*/
        void process();
        /*Returns the description of this event*/
        const char *description() const;
      private: 
        /* The pointer the to AladdinTLB unit*/
        AladdinTLB *tlb;
    };
    
    class outStandingWalkReturnEvent : public Event 
    {
      public: 
        /*Constructs a outStandingWalkReturnEvent*/
        outStandingWalkReturnEvent(AladdinTLB *_tlb);
        /*Processes the event*/
        void process();
        /*Returns the description of this event*/
        const char *description() const;
      private: 
        /* The pointer the to AladdinTLB unit*/
        AladdinTLB *tlb;
    };

    std::deque<PacketPtr> hitQueue;
    std::deque<Addr> outStandingWalks;
    std::unordered_multimap<Addr, PacketPtr> missQueue;
    
  public:
    AladdinTLB(Datapath *_datapath, unsigned _num_entries, unsigned _assoc, Cycles _hit_latency, Cycles _miss_latency, Addr pageBytes);
    ~AladdinTLB();

    std::string name() const;

    void translateTiming(PacketPtr pkt);
    
    void insert(Addr vpn, Addr ppn);
    
    void regStats();
    Stats::Scalar hits;
    Stats::Scalar misses;
    Stats::Formula hitRate;
};

#endif
