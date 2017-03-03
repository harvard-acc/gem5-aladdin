#!/usr/bin/env python
#
# Primary runner for generating design sweeps through the Xenon system.

import argparse

from config_writers import *
from xenon.xenon_interpreter import XenonInterpreter

def run(input_file):
  interpreter = XenonInterpreter(input_file)
  genfiles = interpreter.run()
  writers = [
    aladdin_config_writer.AladdinConfigWriter,
    gem5_config_writer.Gem5ConfigWriter,
    condor_writer.CondorWriter,
  ]
  for genfile in genfiles:
    for writer_type in writers:
      writer = writer_type()
      writer.process(genfile)

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("xenon_file", help="Xenon input file.")
  args = parser.parse_args()

  run(args.xenon_file)

if __name__ == "__main__":
  main()
