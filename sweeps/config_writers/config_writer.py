# Generic gem5 configuration writer base class.

import abc
import json
import os
import re

# Matches against any identifier followed by a parenthesis. Meant to be used
# for identifying class types.
ident_re = re.compile("[A-Za-z0-9_]+(?=\()")

class ConfigWriter(object):
  __metaclass__ = abc.ABCMeta

  def __init__(self):
    # Return the next sweep_id for this benchmark.
    self.next_sweep_id_ = {}

  @abc.abstractmethod
  def write(self, sweep):
    """ Write configuration files for this sweep. """
    pass

  @abc.abstractmethod
  def is_applicable(self, sweep):
    """ Check if this config writer should produce output for this sweep. """
    pass

  def process(self, jsonfile):
    with open(jsonfile, "r") as f:
      sweeps = json.load(f)
    for sweep in sweeps:
      # sweep is a single key-value pair dict.
      (sweep_name, sweep_content), = sweep.items()
      if not self.is_applicable(sweep_content):
        # This configuration writer might not be applicable for the given
        # sweep. Check and skip if so.
        continue

      self.write(sweep_content)

  def getOutputSweepDirectory(self, sweep, benchmark):
    """ Build a common name structure for sweep directories. """
    cwd = os.getcwd()
    path = os.path.join(cwd, sweep["output_dir"], benchmark,
                        "%d" % self.next_id(benchmark))
    return path

  def get_identifier(self, name):
    """ Returns the class name identifier. """
    result = re.search(ident_re, name)
    if result:
      return result.group(0)
    return None

  def next_id(self, benchmark_name):
    if benchmark_name in self.next_sweep_id_:
      sweep_id = self.next_sweep_id_[benchmark_name]
      self.next_sweep_id_[benchmark_name] += 1
      return sweep_id
    else:
      self.next_sweep_id_[benchmark_name] = 0
      return 0

  def itersimpletypes(self, dictionary):
    """ Returns a generator over non-dict and non-list items in dictionary. """
    for param, value in dictionary.iteritems():
      if not isinstance(value, dict) and not isinstance(value, list):
        yield param, value

  def iterdicttypes(self, dictionary):
    """ Returns a generator over dict-type items in dictionary. """
    for param, value in dictionary.iteritems():
      if isinstance(value, dict):
        yield param, value
