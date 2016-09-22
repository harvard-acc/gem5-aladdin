from xenon.base.datatypes import Sweepable
from xenon.base.designsweeptypes import ExhaustiveSweep

import params

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

  def add_array(self, *args):
    """ Add an array of this benchmark.

    Args:
      *args: Array constructor args.
    """
    array = Array(*args)
    assert(not hasattr(self, array.name))
    setattr(self, array.name, array)

  def add_function_array(self, func, *args):
    """ Add an array of this benchmark that does not belong to the kernel.

    For example, if an array 'bar' were declared inside an inner function
    'foo' (where 'foo' is not the top kernel function), the user would have to
    refer to this array as 'foo.bar'.

    Args:
      func: Function name.
      *args: Array constructor args.
    """
    array = Array(*args)
    self.add_function(func)
    getattr(self, func).add_array(func, *args)

  def add_loop(self, function_name, *args):
    """ Add a loop of this benchmark.

    Args:
      function_name: The name of the function that contains this loop.
      *args: Loop constructor args.
    """
    self.add_function(function_name)
    getattr(self, function_name).add_loop(*args)

  def add_function(self, function_name):
    if not hasattr(self, function_name):
      f = Function(function_name)
      setattr(self, function_name, f)

  def set_kernels(self, kernels):
    """ Set the kernels to be traced in this benchmark.

    If a single kernel is provided, then all functions called by that function
    will be called. If multiple kernels are provided, only those functions will
    appear in the dynamic trace.

    Args:
      kernels: A list of function names.
    """
    self.kernels = kernels

  def set_main_id(self, main_id):
    """ Set the id number of this benchmark.

    In a system with multiple accelerators, this allows the simulator to distinguish
    between them.

    TODO: Remove this and replace with a dynamic registration procedure
    (BUG=ALADDIN-66).

    Args:
      main_id: integer id of this benchmark.
    """
    self.main_id = main_id

  # TODO: Leave off exec cmd for now.

class Array(Sweepable):
  sweepable_params = [
      params.partition_type,
      params.partition_factor,
      params.memory_type,
  ]

  def __init__(self, name, size, word_length):
    """ Creates an array. """
    super(Array, self).__init__(name)
    self.size = size
    self.word_length = word_length

class Function(Sweepable):
  sweepable_params = []

  def __init__(self, name):
    super(Function, self).__init__(name)

  def add_array(self, func, *args):
    a = Array(*args)
    assert(not hasattr(self, a.name))
    setattr(self, a.name, a)

  def add_loop(self, *args):
    l = Loop(*args)
    assert(not hasattr(self, l.name))
    setattr(self, l.name, l)

class Loop(Sweepable):
  sweepable_params = [
      params.unrolling,
  ]

  def __init__(self, name):
    """ Creates a loop. """
    super(Loop, self).__init__(name)
