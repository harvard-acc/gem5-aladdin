#!/usr/bin/env python
#
# Primary runner for generating design sweeps through the Xenon system.

import argparse
import os
import re
import json

from xenon.xenon_interpreter import XenonInterpreter
from config_writers import *

def run(input_file):
  interpreter = XenonInterpreter(input_file)
  genfiles = interpreter.run()
  writers = [
    aladdin_config_writer.AladdinConfigWriter,
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
