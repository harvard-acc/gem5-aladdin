#include "aladdin_tlb.hh"
#include "datapath.hh"
#include "debug/Datapath.hh"

AladdinTLB::AladdinTLB(Datapath *_datapath, unsigned _num_entries, unsigned _assoc, Cycles _hit_latency, Cycles _miss_latency, Addr _page_bytes) : 
  datapath(_datapath),
  numEntries(_num_entries), 
  assoc(_assoc),
  hitLatency(_hit_latency), 
  missLatency(_miss_latency), 
  pageBytes(_page_bytes),
  deHitQueueEvent(this),
  deMissQueueEvent(this)
{
  if (numEntries > 0)
    tlbMemory = new TLBMemory (_num_entries, _assoc, _page_bytes);
  else
    tlbMemory = new InfiniteTLBMemory();
}

void
AladdinTLB::translateTiming(PacketPtr pkt)
{
  Addr vaddr = pkt->req->getVaddr();
  DPRINTF(Datapath, "Translating vaddr %#x.\n", vaddr);
  Addr offset = vaddr % pageBytes;
  Addr vpn = vaddr - offset;
  Addr ppn;
  
  if (tlbMemory->lookup(vpn, ppn)) 
  {
      DPRINTF(Datapath, "TLB hit. Phys addr %#x.\n", ppn + offset);
      hits++;
      hitQueue.push_back(pkt);
      datapath->schedule(deHitQueueEvent, datapath->clockEdge(hitLatency));
  } 
  else 
  {
      // TLB miss! Let the TLB handle the walk, etc
      DPRINTF(Datapath, "TLB miss for addr %#x\n", vaddr);
      misses++;
         
      //insert TLB entry; for now, vpn == ppn
      insert(vpn, vpn);
      missQueue.push_back(pkt);
      datapath->schedule(deMissQueueEvent, datapath->clockEdge(missLatency));
  }
}
void 
AladdinTLB::deHitQueue()
{
  assert(!hitQueue.empty());
  datapath->finishTranslation(hitQueue.front());
  hitQueue.pop_front();
}
void 
AladdinTLB::deMissQueue()
{
  assert(!missQueue.empty());
  datapath->finishTranslation(missQueue.front());
  missQueue.pop_front();
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
