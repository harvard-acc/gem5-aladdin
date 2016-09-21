from xenon.base.designsweeptypes import ExhaustiveSweep
from benchmarks.datatypes import DMA, SPAD, CACHE
from generators import trace_generator

class Gem5DesignSweep(ExhaustiveSweep):
  def __init__(self, name):
    super(Gem5DesignSweep, self).__init__(name)
    self.source_dir = ""
    self.memory_type = ""
    self.simulator = ""

  def fixArguments(self):
    """ Some argument adjustments. """
    # Convert memory type to integer flags.
    if self.memory_type == "spad":
      self.memory_type = SPAD
    elif self.memory_type == "dma":
      self.memory_type = DMA | SPAD
    elif self.memory_type == "cache":
      self.memory_type = CACHE
    elif self.memory_type == "hybrid":
      self.memory_type = SPAD | CACHE
    else:
      raise ValueError("The value of memory_type was not recognized.")

  def generate_trace(self):
    self.fixArguments()
    generator = trace_generator.TraceGenerator(self)
    return generator.run()
