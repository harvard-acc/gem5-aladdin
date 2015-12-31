# Imports all detected benchmark suite configurations.

import importlib
import os

modules = os.listdir("benchmark_configs")
benchmarks = {}
for module in modules:
  if module.endswith("_config.py"):
    modulepath = "benchmark_configs.%s" % module[0:-3]
    try:
      i = importlib.import_module(modulepath)
      benchmarks[i._SUITE_NAME] = i._BENCHMARKS
    except (ImportError, AttributeError):
      pass
