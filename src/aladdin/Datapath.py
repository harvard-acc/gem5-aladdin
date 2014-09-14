from m5.params import *
from MemObject import MemObject
from m5.proxy import *

class Datapath(MemObject):
  type = 'Datapath'
  cxx_header = "aladdin/datapath.hh"
  benchName = Param.String("Aladdin Bench Name")
  traceFileName = Param.String("Aladdin Input Trace File")
  configFileName = Param.String("Aladdin Config File")
  cycleTime = Param.Unsigned(6, "Clock Period: 6ns default")
  
  
  tlbEntries = Param.Int(0, "number entries in TLB (0 implies infinite)")

  tlbAssoc = Param.Int(4, "Number of sets in the TLB")

  tlbHitLatency = Param.Cycles(0, "number of cycles for a hit")
  
  tlbMissLatency = Param.Cycles(10, "number of cycles for a miss")

  tlbPageBytes = Param.Int(4096, "Page Size")

  system = Param.System(Parent.any, "system object")
  
  dcache_port = MasterPort("Datapath Data Port")
  _cached_ports = ['dcache_port']
  _uncached_slave_ports = []
  _uncached_master_ports = []
  
  def connectCachedPorts(self, bus):
      for p in self._cached_ports:
          exec('self.%s = bus.slave' % p)

  def connectUncachedPorts(self, bus):
      for p in self._uncached_slave_ports:
          exec('self.%s = bus.master' % p)
      for p in self._uncached_master_ports:
          exec('self.%s = bus.slave' % p)

  def connectAllPorts(self, cached_bus, uncached_bus = None) :
    self.connectCachedPorts(cached_bus)
    if not uncached_bus:
      uncached_bus = cached_bus
    self.connectUncachedPorts(uncached_bus)

  def addPrivateL1Dcache(self, dc, dwc = None) :
    self.dcache = dc
    self.dcache_port = dc.cpu_side
    self._cached_ports = ['dcache.mem_side']

    #if dwc :
      #self.dtb_walker_cache = dwc
      #self.dtb.walker.port = dwc.cpu_side
      #self._cached_ports += ["dtb_walker_cache.mem_side"]
    #else: 
      #self._cached_ports += ["dtb.walker.port"]

