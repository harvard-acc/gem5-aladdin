# Writes Condor scripts to schedule all simulations.

import os

from config_writers import config_writer

header = [
    "#Submit this file in the directory where the input and output files live",
    "Universe        = vanilla",
    "# With the following setting, Condor will try to copy and use the environ-",
    "# ment of the submitter, provided its not too large.",
    "GetEnv          = True",
    "# This forces the jobs to be run on the less crowded server",
    "Requirements    = (OpSys == \"LINUX\") && (Arch == \"X86_64\") && ( TotalMemory > 128000)",
    "# Change the email address to suit yourself",
    "Notification    = Error",
    "Executable = /bin/bash"
]

class CondorWriter(config_writer.JsonConfigWriter):
  def __init__(self):
    super(CondorWriter, self).__init__()
    self.topLevelType = "Benchmark"
    self.output = None

  def is_applicable(self, sweep):
    return ("output_dir" in sweep and "name" in sweep)

  def writeSweep(self, sweep):
    """ Write each sweep job to output. """
    if not self.output:
      # On the first time, write the header.
      condor_script = os.path.join(sweep["output_dir"], "%s.con" % sweep["name"])
      self.output = open(condor_script, "w")
      for line in header:
        self.output.write(line + "\n")
      self.output.write("\n")
    self.writeSweepRecursive_(sweep)

  def writeLast(self, all_sweeps):
    self.output.close()

  def writeSweepRecursive_(self, obj):
    """ Write condor job for each Benchmark obj in the JSON dump. """
    for name, child_obj in self.iterdicttypes(obj):
      identifier = self.get_identifier(name)
      assert(identifier)

      if identifier == self.topLevelType:
        benchmark = child_obj["name"]
        output_dir = self.getOutputSweepDirectory(obj, benchmark)
        if not os.path.exists(output_dir):
          os.makedirs(output_dir)
        runscript = os.path.join(output_dir, "run.sh")
        log = os.path.join(output_dir, "log")
        self.output.write("InitialDir = %s\n" % output_dir)
        self.output.write("Arguments = %s\n" % runscript)
        self.output.write("Log = %s\n" % log)
        self.output.write("Queue\n\n")
      else:
        self.writeRecursive_(child_obj)
