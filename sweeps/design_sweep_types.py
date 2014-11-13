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

# The name of the SweepParam indicates the parameter being swept. The sweep
# begins at @start and ends at @end, stepping by @step. The step type is either
# linear or exponential. For linear, the parameter value is simply incremented
# by the step amount each iteration. For exponential the parameter value is
# multiplied by the step amount instead.
SweepParam = namedtuple("SweepParam", "name, start, end, step, step_type")

# A loop inside a benchmark. It is given a name and a line number in which it
# appears in the source file.
Loop = namedtuple("Loop", "name, line_num, trip_count")

# An array inside a benchmark. Use the array's name and size in the appropriate
# fields. word_size is the size of each element (for ints it is 4).
# partition_type should be set to one of the PARTITION_* constants.
Array = namedtuple("Array", "name, size, word_size, partition_type")

class Benchmark(object):
  """ A benchmark description object. """
  def __init__(self, name, source_file):
    """ Construct a benchmark description object.

    Args:
      name: Informal name of the benchmark.
      source_file: Source code file name, excluding the extension.
    """
    self.name = name
    self.source_file = source_file
    self.loops = []
    self.arrays = []
    self.kernels = []
    # Test harness, if applicable. If used, test_harness is assumed to contain
    # main(); otherwise, source_file is used, and test_harness MUST be the empty
    # string "".
    self.test_harness = ""

  def add_loop(self, loop_name, line_num, trip_count=ALWAYS_UNROLL):
    self.loops.append(Loop(name=loop_name,
                           line_num=line_num,
                           trip_count=trip_count))

  def add_array(self, name, size, word_size, partition_type):
    self.arrays.append(Array(name=name, size=size, word_size=word_size,
                             partition_type=partition_type))

  def set_kernels(self, kernels):
    self.kernels = kernels

  def set_test_harness(self, test_harness):
    self.test_harness = test_harness

if __name__ == "__main__":
  main()
