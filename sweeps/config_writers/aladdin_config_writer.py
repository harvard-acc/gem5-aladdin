# Writes JSON design sweep dumps as Aladdin config files.

import StringIO
import os

from benchmarks import datatypes, params
import config_writer

ALADDIN_PATH = os.path.join(os.environ["ALADDIN_HOME"], "common", "aladdin")

class AladdinConfigWriter(config_writer.ConfigWriter):
  """ Writes JSON design sweep dumps as Aladdin config files. """

  def __init__(self):
    super(AladdinConfigWriter, self).__init__()
    self.topLevelType = "Benchmark"
    self.output = StringIO.StringIO()
    self.printFunctionsMap = {
        "Benchmark": self.printBenchmark,
        "Function": self.printFunction,
        "Array": self.printArray,
        "Loop": self.do_nothing,  # Loops are handled by printFunction.
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

  def write(self, sweep):
    base_output_dir = sweep["output_dir"]
    self.writeRecursive_(sweep)

  def writeRecursive_(self, obj):
    for name, child_obj in self.iterdicttypes(obj):
      identifier = self.get_identifier(name)
      assert(identifier)
      printer = self.getPrintMethod(identifier)
      printer(child_obj)
      self.writeRecursive_(child_obj)

      if identifier == self.topLevelType:
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
          self.output = StringIO.StringIO()
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

  def getPrintMethod(self, identifier):
    return self.printFunctionsMap[identifier]

  def printBenchmark(self, benchmark):
    """ Print benchmark-wide parameters. """
    toplevel_params = [params.cycle_time, params.pipelining, params.ready_mode]
    for param in toplevel_params:
      value = benchmark[param.name]
      self.output.write("%s,%s\n" % (param.name, param.format(value)))

  def printFunction(self, obj):
    """ Print parameters per function. """
    # While functions don't actually have any parameters to themselves, we use
    # this function to print loop unrolling parameters, because the loop
    # unrolling dictionary dumps don't know what function they belong to.
    for loop in obj.iterkeys():
      identifier = self.get_identifier(loop)
      if identifier:
        obj[loop]["func_name"] = obj["name"]
        self.output.write(self.printLoop_(obj[loop]))

  def printArray(self, obj):
    """ Print array partitioning parameters. """
    self.output.write("partition,%(partition_type)s,%(name)s,%(size)d,%(word_length)d" % obj)
    if obj["partition_type"] != "complete":
      self.output.write(",%(partition_factor)s" % obj)
    self.output.write("\n")

  def printLoop_(self, obj):
    """ Print loop unrolling parameters. """
    self.output.write("unrolling,%(func_name)s,%(name)s,%(unrolling)d\n" % obj)

  def do_nothing(*args):
    pass
