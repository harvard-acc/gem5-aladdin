from m5.params import *
from MemObject import MemObject
from m5.proxy import *

class GooUnit(MemObject):
  type = 'GooUnit'
  cxx_header = "goounit/goounit.hh"
  nbCore = Param.Int(1, "number of cores in the processor")
