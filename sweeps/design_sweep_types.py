#!/usr/bin/env python
# Design sweep parameter and benchmark description types.
#
# Author: Sam Xi

from collections import namedtuple

# Sweep parameter setting constants.
NO_SWEEP = -1
LINEAR_SWEEP = 1
EXP_SWEEP = 2
ALWAYS_UNROLL = -1
UNROLL_FLATTEN = 0
UNROLL_ONE = 1
PARTITION_CYCLIC = 1
PARTITION_BLOCK = 2
PARTITION_COMPLETE = 3

# Memory types. Use these for specifying the type of memory to store an array
# in. Combine via bitwise OR (so hybrid is SPAD | CACHE).
SPAD = 0x1
CACHE = 0x2

class SweepParam(namedtuple(
      "SweepParamBase", "name, start, end, step, step_type, short_name, "
      "sweep_per_kernel, link_with")):
  """ SweepParam: A description of a parameter to sweep.

  Args:
    name: Indicates the parameter being swept.
    start: Start the sweep from this value.
    ends: End the sweep at this value, inclusive.
    step: The amount to increase this parameter per sweep.
    type: LINEAR_SWEEP or EXP_SWEEP. Linear sweeps will increment the value by
      @step; For exponential the parameter value is multiplied by the step
      amount instead.
    short_name: Abbreviated name to use for file naming. Defaults to full name.
    sweep_per_kernel: Optional. For multi-kernel accelerators, set this to True
      to sweep this parameter within the kernels themselves, rather than
      applying the value globally.
    link_with: The name of the parameter that this parameter is linked with.
      Linking this parameter with another means that it assumes the same
      values as the linked parameter. The sweep type of this parameter is
      ignored. Be careful with this option - no checking of circular
      references is performed.
  """
  def __new__(cls, name, start, end, step, step_type, short_name=None,
              sweep_per_kernel=False, link_with=None):
    if not short_name:
      short_name = name
    self = super(SweepParam, cls).__new__(
        cls, name, start, end, step, step_type, short_name, sweep_per_kernel,
        link_with)
    # The name of the parameter whose value is controlled by this parameter.
    self.linked_to = None
    return self

# A loop inside a benchmark. It is given a name and a line number in which it
# appears in the source file.
Loop = namedtuple("Loop", "name, line_num, trip_count")

# An array inside a benchmark.
#   Use the array's name and size in the appropriate fields.
#   word_size is the size of each element (for ints it is 4).
#   partition_type should be set to one of the PARTITION_* constants.
#   memory_type should be set to either SPAD or CACHE
Array = namedtuple(
    "Array", "name, size, word_size, partition_type, memory_type")

class Benchmark(object):
  """ A benchmark description object. """
  def __init__(self, name, source_file, harness_file=""):
    """ Construct a benchmark description object.

    Args:
      name: Name of the benchmark.
      source_file: Source code file name, excluding the extension. This should
        be a relative path from the command-line specified source_dir, so that
        the source file is located at <source_dir>/<benchmark_name>/<source_file>.
    """
    self.name = name
    self.source_file = source_file
    self.loops = []
    self.arrays = []
    self.kernels = []
    self.kernel_ids = {}
    self.main_id = 0
    # Command to execute the binary under gem5..
    self.exec_cmd = ""
    # If being run as a binary under gem5, use these arguments to execute the binary.
    self.run_args = ""
    # Test harness, if applicable. If used, test_harness is assumed to contain
    # main(); otherwise, source_file is used, and test_harness MUST be the empty
    # string "".
    self.test_harness = harness_file
    # For more sophisticated benchmark suites, use a Makefile located in the
    # directory of the source file to generate the traces.
    self.makefile = False
    # Produce separate Aladdin configuration files for each kernel. This is
    # useful for composability studies.
    self.separate_kernels = False

  def add_loop(self, loop_name, line_num, trip_count=ALWAYS_UNROLL):
    """ Add a loop, its line number, and its trip count to the benchmark.

    If the loop is not tagged, the loop_name should be the name of the kernel
    function to which it belongs.
    """
    self.loops.append(Loop(name=loop_name,
                           line_num=line_num,
                           trip_count=trip_count))

  def add_array(self, name, size, word_size, partition_type=PARTITION_CYCLIC,
                memory_type=SPAD):
    """ Define an array in the benchmark. Size is in words. """
    self.arrays.append(Array(name=name,
                             size=size,
                             word_size=word_size,
                             memory_type=memory_type,
                             partition_type=partition_type))

  def set_kernels(self, kernels):
    """ Names of the distinct functions/kernels in the benchmark.

    If there are multiple kernels, generate_separate_kernels() has been or will
    be called, and the order in which they are executed matters, then that order
    will be the order of this list. Sequential execution is enforced.
    """
    self.kernels = kernels

  def set_main_id(self, main_id):
    """ Set the starting accelerator code or ioctl request code.

    See src/aladdin/gem5/aladdin_ioctl_req.cpp for more details.
    """
    self.main_id = main_id

  def set_kernel_id(self, kernel, kernel_id):
    self.kernel_ids[kernel] = kernel_id

  def set_test_harness(self, test_harness):
    self.test_harness = test_harness

  def set_exec_cmd(self, cmd):
    self.exec_cmd = cmd

  def set_run_args(self, args):
    self.run_args = args

  def use_local_makefile(self, value=True):
    self.makefile = value

  def generate_separate_kernels(self, separate=True):
    self.separate_kernels = separate
  def get_kernel_id(self, kernel):
    """ Returns the index at which @kernel appears in the list of kernels.

    If not found, this returns -1.
    """
    if kernel in self.kernel_ids:
      return self.kernel_ids[kernel]
    else:
      for i in range(0, len(self.kernels)):
        if self.kernels[i] == kernel:
          return self.main_id + i + 1
      return -1

  def expand_exec_cmd(self, values):
    value_subset = {}
    # Only expand the string for keys that exist.
    for key, value in values.iteritems():
      if "%(" + key + ")s" in self.exec_cmd:
        value_subset[key] = value
    return self.exec_cmd % value_subset

  def expand_run_args(self, values):
    value_subset = {}
    # Only expand the string for keys that exist.
    for key, value in values.iteritems():
      if "%(" + key + ")s" in self.run_args:
        value_subset[key] = value
    return self.run_args % value_subset

if __name__ == "__main__":
  main()
