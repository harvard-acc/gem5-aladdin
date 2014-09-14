#include "aladdin_tlb.hh"
#include "datapath.hh"
#include "debug/Datapath.hh"

AladdinTLB::AladdinTLB(Datapath *_datapath, unsigned _num_entries, unsigned _assoc, Cycles _hit_latency, Cycles _miss_latency, Addr _page_bytes, bool _is_perfect) : 
  datapath(_datapath),
  numEntries(_num_entries), 
  assoc(_assoc),
  hitLatency(_hit_latency), 
  missLatency(_miss_latency), 
  pageBytes(_page_bytes),
  isPerfectTLB(_is_perfect)
{
  if (numEntries > 0)
    tlbMemory = new TLBMemory (_num_entries, _assoc, _page_bytes);
  else
    tlbMemory = new InfiniteTLBMemory();
  hits = 0;
  misses = 0;
}

AladdinTLB::~AladdinTLB()
{
  delete tlbMemory;
}

AladdinTLB::deHitQueueEvent::deHitQueueEvent(AladdinTLB *_tlb)
   : Event(Default_Pri, AutoDelete),
     tlb(_tlb) {}
void
AladdinTLB::deHitQueueEvent::process()
{
  assert(!tlb->hitQueue.empty());
  tlb->datapath->finishTranslation(tlb->hitQueue.front());
  tlb->hitQueue.pop_front();
}
const char *
AladdinTLB::deHitQueueEvent::description() const
{
  return "TLB Hit";
}

AladdinTLB::outStandingWalkReturnEvent::outStandingWalkReturnEvent(AladdinTLB *_tlb)
   : Event(Default_Pri, AutoDelete),
     tlb(_tlb) {}
void
AladdinTLB::outStandingWalkReturnEvent::process()
{
  assert(!tlb->missQueue.empty());
  Addr vpn = tlb->outStandingWalks.front();
  //insert TLB entry; for now, vpn == ppn
  tlb->insert(vpn, vpn);
  
  auto range = tlb->missQueue.equal_range(vpn);
  for(auto it = range.first; it!= range.second; ++it)
    tlb->datapath->finishTranslation(it->second);
  
  tlb->missQueue.erase(vpn);
  tlb->outStandingWalks.pop_front();
}
const char *
AladdinTLB::outStandingWalkReturnEvent::description() const
{
  return "TLB Miss";
}


void
AladdinTLB::translateTiming(PacketPtr pkt)
{
  Addr vaddr = pkt->req->getPaddr();
  DPRINTF(Datapath, "Translating vaddr %#x.\n", vaddr);
  Addr offset = vaddr % pageBytes;
  Addr vpn = vaddr - offset;
  Addr ppn;
  
  if (isPerfectTLB || tlbMemory->lookup(vpn, ppn)) 
  {
      DPRINTF(Datapath, "TLB hit. Phys addr %#x.\n", ppn + offset);
      hits++;
      hitQueue.push_back(pkt);
      deHitQueueEvent *hq = new deHitQueueEvent(this);
      datapath->schedule(hq, datapath->clockEdge(hitLatency));
  } 
  else 
  {
      // TLB miss! Let the TLB handle the walk, etc
      DPRINTF(Datapath, "TLB miss for addr %#x\n", vaddr);
      misses++;
         
      if (missQueue.find(vpn) == missQueue.end())
      {
        outStandingWalks.push_back(vpn);
        outStandingWalkReturnEvent *mq = new outStandingWalkReturnEvent(this);
        datapath->schedule(mq, datapath->clockEdge(missLatency));
      }
      missQueue.insert({vpn, pkt});

  }
}

void
AladdinTLB::insert(Addr vpn, Addr ppn)
{
    tlbMemory->insert(vpn, ppn);
}

std::string
AladdinTLB::name() const
{
  return datapath->name() + ".tlb";
}
bool
TLBMemory::lookup(Addr vpn, Addr& ppn, bool set_mru)
{
    int way = (vpn / pageBytes) % ways;
    for (int i=0; i < sets; i++) {
        if (entries[way][i].vpn == vpn && !entries[way][i].free) {
            ppn = entries[way][i].ppn;
            assert(entries[way][i].mruTick > 0);
            if (set_mru) {
                entries[way][i].setMRU();
            }
            entries[way][i].hits++;
            return true;
        }
    }
    ppn = Addr(0);
    return false;
}

void
TLBMemory::insert(Addr vpn, Addr ppn)
{
    Addr a;
    if (lookup(vpn, a)) {
        return;
    }
    int way = (vpn / pageBytes) % ways;
    AladdinTLBEntry* entry = NULL;
    Tick minTick = curTick();
    for (int i=0; i < sets; i++) {
        if (entries[way][i].free) {
            entry = &entries[way][i];
            break;
        } else if (entries[way][i].mruTick < minTick) {
            minTick = entries[way][i].mruTick;
            entry = &entries[way][i];
        }
    }
    assert(entry);
    if (!entry->free) {
        DPRINTF(Datapath, "Evicting entry for vpn %#x\n", entry->vpn);
    }

    entry->vpn = vpn;
    entry->ppn = ppn;
    entry->free = false;
    entry->setMRU();
}
/*
void
AladdinTLB::regStats()
{
    hits
        .name(name()+".hits")
        .desc("Number of hits in this TLB")
        ;
    misses
        .name(name()+".misses")
        .desc("Number of misses in this TLB")
        ;
    hitRate
        .name(name()+".hitRate")
        .desc("Hit rate for this TLB")
        ;

    hitRate = hits / (hits + misses);
}
*/
