#!/usr/bin/env python
#
# Hack to enable incremental builds on Travis CI.
#
# This script kills a build if it hasn't finished after N minutes. If the build
# was not completed, then this script returns a zero returncode but does not
# run the tests. The build must be manually restarted, and build products must
# be cached in Travis. After enough times of manually restarting the build,
# the build will finish, and only then will the integration tests be run.

import os
import signal
import subprocess
import sys
import threading
import unittest

# Import all unit tests into this module.
sys.path.append(os.path.join(os.getcwd(), "src", "aladdin", "integration-test", "common"))
import run_cpu_tests
import run_ruby_tests

TIMEOUT_SECS = 40*60
CMD = "scons build/X86/gem5.opt --ignore-style -j2"

def timeout_func(proc):
  print("Build canceled before Travis CI times out...")
  # Kill the entire process group (since we do parallel builds).
  os.killpg(os.getpgid(proc.pid), signal.SIGINT)

def run_with_timeout(cmd, timeout_sec):
  """ Run the command for up to timeout_sec.

  If the process does not complete in that time, it is sent SIGINT.

  Returns the return code.
  """
  proc = subprocess.Popen(cmd, stderr=subprocess.STDOUT, shell=True, preexec_fn=os.setsid)
  timer = threading.Timer(timeout_sec, timeout_func, [proc])
  try:
    timer.start()
    stdout, stderr = proc.communicate()
  finally:
    timer.cancel()
    print("Return code was %d" % proc.returncode)

  return proc.returncode

def create_variables_file():
  contents = ("TARGET_ISA = 'x86'\n"
              "CPU_MODELS = 'AtomicSimpleCPU,O3CPU,TimingSimpleCPU'\n"
              "PROTOCOL = 'MESI_Two_Level_aladdin'\n")
  if not os.path.exists("build/variables"):
    os.makedirs("build/variables")
  with open("build/variables/X86", "w") as f:
    f.write(contents)

def main():
  create_variables_file()
  ret = run_with_timeout(CMD, TIMEOUT_SECS)
  # SIGINT is the one we send and want to fake as success, but we don't want to
  # run the integration tests. Anything else than 0 should be an actual build
  # error.
  if ret == -signal.SIGINT:
    return 0
  elif ret != 0:
    return ret

  suite = unittest.TestSuite()
  suite.addTests(unittest.TestLoader().loadTestsFromModule(run_cpu_tests))
  suite.addTests(unittest.TestLoader().loadTestsFromModule(run_ruby_tests))
  result = unittest.TextTestRunner(verbosity=2).run(suite)
  return 0 if result.wasSuccessful() else 1

if __name__ == "__main__":
  sys.exit(main())
