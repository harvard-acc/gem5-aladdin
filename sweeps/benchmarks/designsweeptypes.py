# Design sweep class for gem5.

import os

from xenon.base.designsweeptypes import ExhaustiveSweep

from benchmarks import params
from generators import *

class Gem5DesignSweep(ExhaustiveSweep):
  sweepable_params = [
      # gem5 needs to know this to determine whether a cache should be attached
      # to the accelerator.
      params.memory_type
  ]

  def __init__(self, name):
    super(Gem5DesignSweep, self).__init__(name)

    # Path to the source directory of the benchmark suite being swept.
    # TODO: Find a way to encapsulate this in benchmark configurations without
    # having it tied to Git history.
    self.source_dir = ""

    # Simulation mode.
    # Valid options are:
    #   - aladdin: Run Aladdin only.
    #   - gem5-cpu: Run Aladdin in conjunction with a gem5 CPU model. In this case,
    #     Aladdin must be invoked by the user program running on the CPU.
    self.simulator = ""

  def validate(self):
    super(Gem5DesignSweep, self).validate()
    if not os.path.exists(self.source_dir):
      raise IOError("Source directory %s does not exist!" % self.source_dir)

    valid_simulators = ["gem5-cpu", "aladdin"]
    if not self.simulator in valid_simulators:
      raise ValueError("Attribute simulator has invalid value %s." % (
          self.simulator, valid_simulators))

  def generate_trace(self):
    generator = trace_generator.TraceGenerator(self)
    return generator.run()

  def generate_dma_trace(self):
    generator = trace_generator.TraceGenerator(self, dma=True)
    return generator.run()

  def generate_gem5_binary(self):
    generator = gem5_binary_generator.Gem5BinaryGenerator(self)
    return generator.run()
