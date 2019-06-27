from m5.params import *
from m5.objects import CommMonitor, Cache, MemTraceProbe
from MemObject import MemObject
from m5.proxy import *
from XBar import *

class SystolicArray(MemObject):
  type = 'SystolicArray'
  cxx_header = "systolic_array/systolic_array.h"

  acceleratorName = Param.String("", "Unique accelerator name")
  acceleratorId = Param.Int(-1, "Accelerator Id")
  spad_port = MasterPort("DMA port")
  cache_port = MasterPort("Cache coherent port")
  system = Param.System(Parent.any, "System object")

  # DMA port parameters
  maxDmaRequests = Param.Unsigned(16, "Max number of outstanding DMA requests")
  numDmaChannels = Param.Unsigned(16, "Number of virtual DMA channels.")
  dmaChunkSize = Param.Unsigned("64", "DMA transaction chunk size.")
  invalidateOnDmaStore = Param.Bool(
      True, "Invalidate the region of memory "
      "that will be modified by a dmaStore before issuing the DMA request.")
  recordMemoryTrace = Param.Bool(
      False, "Record memory traffic going to/from the accelerator.")

  # Systolic array attributes.
  peArrayRows = Param.Unsigned(8, "Number of PEs per row.")
  peArrayCols = Param.Unsigned(8, "Number of PEs per column.")
  sramSize = Param.Unsigned(32768, "Size of the SRAM.")

  def connectThroughMonitor(self, monitor_name, master_port, slave_port):
    """ Connect the master and slave port through a CommMonitor. """
    trace_file_name = "memory_trace.gz"
    monitor = CommMonitor.CommMonitor()
    monitor.trace = MemTraceProbe.MemTraceProbe(trace_file=trace_file_name)
    monitor.slave = master_port
    monitor.master = slave_port
    setattr(self, monitor_name, monitor)

  def connectPrivateScratchpad(self, system, bus):
    if self.recordMemoryTrace:
      monitor_name = "spad_monitor"
      self.connectThroughMonitor(monitor_name, self.spad_port, bus.slave)
    else:
      self.spad_port = bus.slave

  def addPrivateL1Dcache(self, system, bus, dwc = None):
    self.cache_port = self.cache.cpu_side

    if self.recordMemoryTrace:
      monitor_name = "cache_monitor"
      self.connectThroughMonitor(monitor_name, self.cache.mem_side, bus.slave)
    else:
      self.cache.mem_side = bus.slave
