from m5.params import *
from m5.objects import CommMonitor, Cache, MemTraceProbe
from MemObject import MemObject
from m5.proxy import *
from XBar import *

# A bus that connects the fetch units and the scratchpad.
class SpadXBar(NoncoherentXBar):
  # 128-bit crossbar by default
  width = 16

  # Assume a simpler datapath than a coherent crossbar, incurring
  # less pipeline stages for decision making and forwarding of
  # requests.
  frontend_latency = 1
  forward_latency = 1
  response_latency = 1

class Scratchpad(ClockedObject):
  type = "Scratchpad"
  cxx_class = "systolic::Scratchpad"
  cxx_header = "systolic_array/scratchpad.h"
  size = Param.Int(32768, "Size of the scratchpad in bytes.")
  addrRanges = VectorParam.AddrRange(
      [AllMemory], "Address range this controller responds to")
  lineSize = Param.Int(8, "Line size of the scratchpad.")
  numBanks = Param.Int(16, "Number of banks.")
  numPorts = Param.Int(1, "Number of ports.")
  partType = Param.String("cyclic", "Partition type of the scratchpad.")
  accelSidePort = SlavePort("Port that goes to the accelerator.")

class SystolicArray(ClockedObject):
  type = 'SystolicArray'
  cxx_class = "systolic::SystolicArray"
  cxx_header = "systolic_array/systolic_array.h"

  acceleratorName = Param.String("", "Accelerator name")
  acceleratorId = Param.Int(-1, "Accelerator Id")
  spad_port = MasterPort("DMA port")
  cache_port = MasterPort("Cache coherent port")
  acp_port = MasterPort("ACP port")
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

  # Cache parameters.
  # This small cache is for the finish flag to be communicated via the shared
  # memory.
  cacheSize = Param.String("128B", "Private cache size")
  cacheLineSize = Param.Int("32", "Cache line size (in bytes)")
  cacheAssoc = Param.Int(2, "Private cache associativity")

  # Systolic array attributes.
  peArrayRows = Param.Unsigned(8, "Number of PEs per row.")
  peArrayCols = Param.Unsigned(8, "Number of PEs per column.")
  dataType = Param.String("float32", "Data type of the accelerator.")
  fetchQueueCapacity = Param.Unsigned(
      8, "Capacity of the queue in the fetch unit.")
  commitQueueCapacity = Param.Unsigned(
      8, "Capacity of the queue in the commit unit.")
  lineSize = Param.Unsigned(
      8, "Line size of the data stored in the scratchpads.")

  # Scratchpads.
  inputSpad = Param.Scratchpad("Local input scratchpad.")
  weightSpad = Param.Scratchpad("Local weight scratchpad.")
  outputSpad = Param.Scratchpad("Local weight scratchpad.")
  input_spad_port = VectorMasterPort(
      "Ports from the fetch units to the input scratchpad.")
  weight_spad_port = VectorMasterPort(
      "Ports from the fetch units to the weight scratchpad.")
  output_spad_port = VectorMasterPort(
      "Ports from the commit units to the output scratchpad.")
  inputSpadBus = SpadXBar()
  weightSpadBus = SpadXBar()
  outputSpadBus = SpadXBar()

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
