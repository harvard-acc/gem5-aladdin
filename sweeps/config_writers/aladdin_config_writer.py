# Writes JSON design sweep dumps as Aladdin config files.

from io import StringIO
import os

from benchmarks import datatypes, params
from config_writers import config_writer

ALADDIN_PATH = os.path.join(os.environ["ALADDIN_HOME"], "common", "aladdin")

class AladdinConfigWriter(config_writer.JsonConfigWriter):
  """ Writes JSON design sweep dumps as Aladdin config files. """

  def __init__(self):
    super(AladdinConfigWriter, self).__init__()
    self.topLevelType = "Benchmark"
    self.output = StringIO()
    self.printFunctionsMap = {
        "Benchmark": self.printBenchmark,
        "Function": self.doNothing,
        "Array": self.printArray,
        "Loop": self.printLoop,
    }
    self.generate_runscripts = False

  def is_applicable(self, sweep):
    if not "simulator" in sweep:
      return False
    simulator = sweep["simulator"]
    self.generate_runscripts = (simulator == "aladdin")
    return (simulator == "aladdin" or
            simulator == "gem5-cache" or
            simulator == "gem5-cpu")

  def writeSweep(self, sweep):
    self.writeSweepRecursive_(None, sweep)

  def writeLast(self, all_sweeps):
    pass

  def writeSweepRecursive_(self, parent, obj):
    for name, child_obj in self.iterdicttypes(obj):
      # Replace with check for type.
      child_type = child_obj["type"]
      printer = self.getPrintMethod(child_type)
      printer(obj, child_obj)
      self.writeSweepRecursive_(obj, child_obj)

      if child_type == self.topLevelType:
        # The top level object has to prepare the output directories
        # and actually dump the output.
        benchmark = child_obj["name"]
        output_dir = self.getOutputSweepDirectory(obj, benchmark)
        if not os.path.exists(output_dir):
          os.makedirs(output_dir)
        output_file = os.path.join(output_dir, "%s.cfg" % benchmark)
        with open(output_file, "w") as f:
          f.write(self.output.getvalue())
          self.output.close()
          self.output = StringIO()
        if self.generate_runscripts:
          self.writeRunscript(benchmark, output_file)

  def writeRunscript(self, benchmark_name, config_file):
    """ Generate a run.sh to run an Aladdin simulation. """
    config_dir = os.path.dirname(config_file)
    output_dir = os.path.join(config_dir, "outputs")
    if not os.path.exists(output_dir):
      os.makedirs(output_dir)
    output_path = os.path.join("outputs", benchmark_name)
    dynamic_trace_path = os.path.join("..", "inputs", "dynamic_trace.gz")
    config_file = os.path.basename(config_file)
    stdout_path = os.path.join("outputs", "stdout")
    stderr_path = os.path.join("outputs", "stderr")
    runscript_path = os.path.join(config_dir, "run.sh")

    with open(runscript_path, "w") as f:
      lines = ["#!/bin/sh",
               ALADDIN_PATH,
               output_path,
               dynamic_trace_path,
               config_file,
               ">" + stdout_path,
               "2>" + stderr_path,
               ]
      f.write(" \\\n".join(lines))

  def getPrintMethod(self, obj_type):
    return self.printFunctionsMap[obj_type]

  def printBenchmark(self, parent, benchmark):
    """ Print benchmark-wide parameters. """
    toplevel_params = [params.cycle_time, params.pipelining, params.ready_mode]
    for param in toplevel_params:
      value = benchmark[param.name]
      self.output.write("%s,%s\n" % (param.name, param.format(value)))

  def printFunction(self, parent, func):
    """ Print parameters per function. """
    return

  def printArray(self, parent, array):
    """ Print array partitioning parameters. """
    # Edit the array name if the parent is a function.
    if parent["type"] == "Function":
      array["name"] = "{0}.{1}".format(parent["name"], array["name"])

    is_host_array = array["is_host_array"]
    if array["memory_type"] == params.SPAD and not is_host_array:
      str_format = "partition,%(partition_type)s,%(name)s,%(size)d,%(word_length)d"
      if (array["partition_type"] == params.CYCLIC or
          array["partition_type"] == params.BLOCK):
        str_format += ",%(partition_factor)s"
    elif array["memory_type"] == params.CACHE and is_host_array:
      str_format = "cache,%(name)s,%(size)d,%(word_length)d"
    else:
      return

    array["size"] = array["size"] * array["word_length"]
    self.output.write(str_format % array)
    self.output.write("\n")

  def printLoop(self, parent, loop):
    """ Print loop unrolling parameters. """
    loop["func_name"] = parent["name"]
    self.output.write("unrolling,%(func_name)s,%(name)s,%(unrolling)d\n" % loop)

  def doNothing(*args):
    pass
