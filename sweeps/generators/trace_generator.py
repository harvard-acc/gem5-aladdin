import os
import subprocess

from xenon.base.datatypes import *
from benchmarks.datatypes import *

DYNAMIC_TRACE = "dynamic_trace.gz"

class TraceGenerator(object):
  def __init__(self, sweep):
    # The configured design sweep object.
    self.sweep = sweep

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
    fdevnull = open(os.devnull, "w")
    cwd = os.getcwd()
    for benchmark in self.sweep.iterattrvalues(objtype=Sweepable):
      assert(isinstance(benchmark, Benchmark))
      print "Building traces for", benchmark.name
      bmk_source_dir = os.path.join(self.sweep.source_dir, benchmark.sub_dir);
      trace_orig_path = os.path.join(bmk_source_dir, DYNAMIC_TRACE)
      os.chdir(bmk_source_dir)
      trace_target = "trace-binary" if not self.sweep.memory_type & DMA else "dma-trace-binary"

      # Clean, rebuild the instrumented binary, and regenerate the trace.
      ret = subprocess.call("make clean-trace",
                            stdout=fdevnull, stderr=subprocess.STDOUT, shell=True)
      if ret:
        print "[ERROR] Failed to clean the existing trace."
        continue
      ret = subprocess.call("make %s" % trace_target,
                            stdout=fdevnull, stderr=subprocess.STDOUT, shell=True)
      if ret:
        print "[ERROR] Failed to build the instrumented binary. Skipping trace generation."
        continue
      ret = subprocess.call("make run-trace",
                            stdout=fdevnull, stderr=subprocess.STDOUT, shell=True)
      if ret:
        print "[ERROR] Failed to execute the instrumented binary and generate the trace."
        continue

      # Move them to the right place.
      trace_abs_dir = os.path.abspath(
          os.path.join(cwd, self.sweep.output_dir, benchmark.name, "inputs"))
      if not os.path.isdir(trace_abs_dir):
        os.makedirs(trace_abs_dir)
      # Moves and overwrites a trace at the destination if it already exists.
      trace_new_path = os.path.join(trace_abs_dir, DYNAMIC_TRACE)
      os.rename(trace_orig_path, trace_new_path)

      # Cleanup.
      ret = subprocess.call("make clean-trace",
                            stdout=fdevnull, stderr=subprocess.STDOUT, shell=True)
      if ret:
        print "[ERROR] Failed to clean up after building and generating traces."

    fdevnull.close()
    os.chdir(cwd)
    return []
