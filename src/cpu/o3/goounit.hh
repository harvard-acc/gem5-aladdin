#include <stdint.h>

#include "base/types.hh"
#include "base/trace.hh"
#include "mem/mem_object.hh"
#include "sim/clocked_object.hh"
#include "params/GooUnit.hh"

class GooUnit: public MemObject
{
  public:
    typedef GooUnitParams Params;
    GooUnit(const Params *p);
    ~GooUnit();
    //void init();
  
  private:
    uint64_t nbCore;
    uint64_t gooCycles;
    void tick();
    EventWrapper<GooUnit, &GooUnit::tick> tickEvent;
};
