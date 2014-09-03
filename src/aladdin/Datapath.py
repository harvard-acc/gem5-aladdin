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

