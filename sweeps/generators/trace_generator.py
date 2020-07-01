import os
import subprocess
import tempfile
import shutil

from xenon.base.datatypes import *
from xenon.generators import base_generator
from benchmarks.datatypes import *

DYNAMIC_TRACE = "dynamic_trace.gz"

class TraceGenerator(base_generator.Generator):
  def __init__(self, sweep, dma=False):
    # The configured design sweep object.
    super(TraceGenerator, self).__init__()
    self.sweep = sweep
    self.dma_mode = dma

  def run(self):
    """ Generates dynamic traces for each workload.

    This is accomplished by invoking the benchmark suite's own Makefiles.  The
    Makefiles must implement the following build targets:

      1. clean-trace: Deletes any existing traces, object files, LLVM IR, and any
         other intermediate files associated with building the instrumented binary.
      2. trace-binary: Build the instrumented binary.
      3. dma-trace-binary: Same as above, but with -DDMA_MODE in CFLAGS.
      4. run-trace: Execute the instrumented binary to produce the trace
         (dynamic_trace.gz).
    """
    if not "TRACER_HOME" in os.environ:
      raise Exception("Set TRACER_HOME directory as an environment variable")
    build_errors_fd, build_errors_path = tempfile.mkstemp()
    with open(build_errors_path, "r+") as build_errors_f:
      cwd = os.getcwd()
      genfiles = []
      for benchmark in self.sweep.iterattrvalues(objtype=Sweepable):
        os.chdir(cwd)
        assert(isinstance(benchmark, Benchmark))
        print("Building %s traces for %s" % (
            "DMA" if self.dma_mode else "non-DMA", benchmark.name))
        bmk_source_dir = os.path.join(self.sweep.source_dir, benchmark.sub_dir)
        if not os.path.isabs(bmk_source_dir):
          bmk_source_dir = os.path.join(cwd, bmk_source_dir)
        os.chdir(bmk_source_dir)
        trace_orig_path = os.path.join(bmk_source_dir, DYNAMIC_TRACE)
        if self.dma_mode:
          trace_target = "dma-trace-binary"
        else:
          trace_target = "trace-binary"

        # Clean, rebuild the instrumented binary, and regenerate the trace.
        ret = subprocess.call("make clean-trace",
                              stdout=build_errors_f, stderr=subprocess.STDOUT, shell=True)
        if ret:
          self.handle_error("Failed to clean the existing trace.", build_errors_f)

        ret = subprocess.call("make %s" % trace_target,
                              stdout=build_errors_f, stderr=subprocess.STDOUT, shell=True)
        if ret:
          self.handle_error(
              "Failed to build the instrumented binary. Skipping trace generation.", build_errors_f)

        ret = subprocess.call("make run-trace",
                              stdout=build_errors_f, stderr=subprocess.STDOUT, shell=True)
        if ret:
          self.handle_error(
              "Failed to execute the instrumented binary and generate the trace.", build_errors_f)

        # Move them to the right place.
        trace_abs_dir = os.path.abspath(
            os.path.join(cwd, self.sweep.output_dir, benchmark.name, "inputs"))
        if not os.path.isdir(trace_abs_dir):
          os.makedirs(trace_abs_dir)
        # Moves and overwrites a trace at the destination if it already exists.
        trace_new_path = os.path.join(trace_abs_dir, DYNAMIC_TRACE)
        shutil.move(trace_orig_path, trace_new_path)
        genfiles.append(trace_new_path)

        # Cleanup.
        ret = subprocess.call("make clean-trace",
                              stdout=build_errors_f, stderr=subprocess.STDOUT, shell=True)
        if ret:
          self.handle_error(
              "Failed to clean up after building and generating traces.", build_errors_f)

    os.chdir(cwd)
    return genfiles
