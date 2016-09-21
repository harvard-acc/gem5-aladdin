# Design sweep class for gem5.

import os

from xenon.base.designsweeptypes import ExhaustiveSweep
import xenon.base.exceptions as xe
from benchmarks.datatypes import DMA, SPAD, CACHE
from generators import trace_generator

class Gem5DesignSweep(ExhaustiveSweep):
  def __init__(self, name):
    super(Gem5DesignSweep, self).__init__(name)

    # Path to the source directory of the benchmark suite being swept.
    # TODO: Find a way to encapsulate this in benchmark configurations without
    # having it tied to Git history.
    self.source_dir = ""

    # The memory system for this simulation.
    # TODO: Implement automatically setting partition_type for all arrays based
    #   on these parameters (by changing parameter defaults).
    # If this option is changed, then traces may need to be rebuilt.
    # Valid options are:
    #   - spad: Private scratchpads. Assume all data is already present.
    #   - dma: Private scratchpads, but use DMA to load them.
    #   - cache: Hardware-managed caches with virtual memory for all accelerator memory.
    #   - hybrid: Use DMA and caches together. The 'partition_type' attribute of an array
    #     determines whether it is cached or stored in scratchpads.
    self.memory_type = ""

    # Simulation mode.
    # Valid options are:
    #   - aladdin: Run Aladdin only.
    #   - gem5-cache: Run Aladdin connected to gem5's memory system, but no CPU.
    #   - gem5-cpu: Run Aladdin in conjunction with a gem5 CPU model. In this case,
    #     Aladdin must be invoked by the user program running on the CPU.
    self.simulator = ""

  def validate(self):
    super(Gem5DesignSweep, self).validate()
    if not os.path.exists(self.source_dir):
      raise IOError("Source directory %s does not exist!" % self.source_dir)

    valid_simulators = ["gem5-cpu", "gem5-cache", "aladdin"]
    if not self.simulator in valid_simulators:
      raise xe.XenonInvalidAttributeError(
          "Attribute simulator has invalid value %s." % self.simulator, valid_simulators)

    valid_memory_types = ["spad", "cache", "dma", "hybrid"]
    if not self.memory_type in valid_memory_types:
      raise xe.XenonInvalidAttributeError(
          "Attribute memory_type has invalid value %s." % self.memory_type, valid_memory_types)

    # There are only specific combinations of simulator and memory_type that
    # are valid.
    if ((self.simulator == "aladdin" and self.memory_type != "spad") or 
        (self.simulator != "aladdin" and self.memory_type == "spad")):
      raise xe.XenonInvalidAttributeError(
          ("Attribute simulator=aladdin is only compatible with "
           "memory_type=spad (and vice versa)."), ["spad"])

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
