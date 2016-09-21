from xenon.base.datatypes import Sweepable
from xenon.base.designsweeptypes import ExhaustiveSweep

import params

# Memory system flags.
SPAD = 0x1
CACHE = 0x2
DMA = 0x4

class Benchmark(Sweepable):
  sweepable_params = [
      params.cycle_time,
      params.pipelining,
      params.cache_size,
      params.cache_assoc,
      params.cache_hit_latency,
      params.cache_line_sz,
      params.tlb_hit_latency,
      params.tlb_miss_latency,
      params.tlb_page_size,
      params.tlb_entries,
      params.tlb_max_outstanding_walks,
      params.tlb_assoc,
      params.tlb_bandwidth,
      params.load_bandwidth,
      params.store_bandwidth,
      params.l2cache_size,
      params.enable_l2,
      params.perfect_l1,
      params.perfect_bus,
      params.pipelined_dma,
      params.ready_mode,
      params.dma_multi_channel,
      params.ignore_cache_flush,
  ]

  def __init__(self, name, source_dir):
    super(Benchmark, self).__init__(name)
    self.sub_dir = source_dir
    self.kernels = []
    self.main_id = 0
    self.exec_cmd = ""
    self.run_args = ""

  def add_array(self, array_name, array_size, array_word_length, **kwargs):
    array = Array(array_name, array_size, array_word_length, **kwargs)
    assert(not hasattr(self, array_name))
    setattr(self, array_name, array)

  def add_loop(self, function_name, loop_name, **kwargs):
    if not hasattr(self, function_name):
      f = Function(function_name)
      setattr(self, function_name, f)
    getattr(self, function_name).add_loop(loop_name, **kwargs)

  def set_kernels(self, kernels):
    self.kernels = kernels

  def set_main_id(self, id):
    pass

  # TODO: Leave off exec cmd for now.

class Array(Sweepable):
  sweepable_params = [
      params.partition_type,
      params.partition_factor,
  ]

  def __init__(self, name, size, word_length, ptype=None, pfactor=None):
    super(Array, self).__init__(name)
    self.size = size
    self.word_length = word_length
    self.partition_type = ptype
    self.partition_factor = pfactor

class Function(Sweepable):
  sweepable_params = []

  def __init__(self, name):
    super(Function, self).__init__(name)

  def add_loop(self, loop_name, **kwargs):
    assert(not hasattr(self, loop_name))
    l = Loop(loop_name, **kwargs)
    setattr(self, loop_name, l)

class Loop(Sweepable):
  sweepable_params = [
      params.unrolling,
  ]

  def __init__(self, name, unr=None):
    super(Loop, self).__init__(name)
    self.unrolling = unr
