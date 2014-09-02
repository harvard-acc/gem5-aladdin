#include "debug/O3CPU.hh"
#include "cpu/o3/goounit.hh"

GooUnit::GooUnit (const Params *p) : 
  MemObject(p), nbCore(p->nbCore),
  tickEvent(this)
{
  gooCycles = 0;
  schedule(tickEvent, clockEdge(Cycles(1)));
  DPRINTF(O3CPU, "Initializing goounit tick @ gooCycle:%d\n", gooCycles);
}

GooUnit::~GooUnit()
{}
/*
void
GooUnit::init()
{
  gooCycles = 0;
}
*/

void
GooUnit::tick()
{
  gooCycles++;
  schedule(tickEvent, clockEdge(Cycles(1)));
  DPRINTF(O3CPU, "Scheduling goounit tick @ gooCycle:%d\n", gooCycles);
}

////////////////////////////////////////////////////////////////////////////
//
//  The SimObjects we use to get the GooUnit information into the simulator
//
////////////////////////////////////////////////////////////////////////////

GooUnit *
GooUnitParams::create()
{
  return new GooUnit(this);
}
