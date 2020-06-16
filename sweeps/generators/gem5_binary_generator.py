import os
import subprocess
import sys
import tempfile

from xenon.base.datatypes import *
from xenon.generators import base_generator
from benchmarks.datatypes import *

class Gem5BinaryGenerator(base_generator.Generator):
  def __init__(self, sweep):
    super(Gem5BinaryGenerator, self).__init__()
    self.sweep = sweep

  def run(self):
    if not "TRACER_HOME" in os.environ:
      raise Exception("Set TRACER_HOME directory as an environment variable")
    build_errors_fd, build_errors_path = tempfile.mkstemp()
    with open(build_errors_path, "r+") as build_errors_f:
      cwd = os.getcwd()
      genfiles = []
      for benchmark in self.sweep.iterattrvalues(objtype=Sweepable):
        assert(isinstance(benchmark, Benchmark))
        print("Building gem5 binaries for %s" % benchmark.name)
        bmk_source_dir = os.path.join(self.sweep.source_dir, benchmark.sub_dir)
        if not os.path.isabs(bmk_source_dir):
          bmk_source_dir = os.path.join(cwd, bmk_source_dir)
        os.chdir(bmk_source_dir)

        ret = subprocess.call("make clean-gem5",
                              stdout=build_errors_f, stderr=subprocess.STDOUT, shell=True)
        if ret:
          self.handle_error("Failed to clean the existing gem5 build.", build_errors_f)

        ret = subprocess.call("make gem5-cpu",
                              stdout=build_errors_f, stderr=subprocess.STDOUT, shell=True)
        if ret:
          self.handle_error("Failed to build non-accelerated gem5 binary.", build_errors_f)
        else:
          genfiles.append("%s-gem5" % benchmark.name)

        ret = subprocess.call("make gem5-accel",
                              stdout=build_errors_f, stderr=subprocess.STDOUT, shell=True)
        if ret:
          self.handle_error("Failed to build accelerated gem5 binary.", build_errors_f)
        else:
          genfiles.append("%s-gem5-accel" % benchmark.name)

        # Don't physically move them anywhere. We'll symlink them later.
    os.chdir(cwd)
    return genfiles
