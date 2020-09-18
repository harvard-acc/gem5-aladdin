from m5.params import *
from m5.proxy import *
from m5.objects.Device import PioDevice

class GlobalScratchpad(PioDevice):
  type = 'GlobalScratchpad'
  cxx_header = 'dev/global_scratchpad.hh'

  # Put at 8GB line by default.
  pio_addr = Param.Addr(0x200000000, "Device Address")
  # The default scratchpad size is 1MB.
  pio_size = Param.Int(1048576, "Scratchpad size")
