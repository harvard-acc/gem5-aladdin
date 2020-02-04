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

MAXIMUM_RETRIES = 1
TIMEOUT = 40  # Minutes.
CMD = "scons build/X86/gem5.opt --ignore-style -j2"

def run_with_timeout(cmd, timeout_sec):
  """ Run the command for up to timeout_sec.

  If the process does not complete in that time, it is sent SIGINT.

  Returns the return code.
  """
  proc = subprocess.Popen(cmd, stderr=subprocess.STDOUT, shell=True, preexec_fn=os.setsid)
  # Kill the entire process group (since we do parallel builds).
  kill_pg = lambda proc: os.killpg(os.getpgid(proc.pid), signal.SIGINT)
  timer = threading.Timer(timeout_sec, kill_pg, [proc])
  try:
    timer.start()
    stdout, stderr = proc.communicate()
  finally:
    timer.cancel()
    print("Build canceled before Travis CI times out...")
    print("Return code was %d" % proc.returncode)

  return proc.returncode

def run_build(cmd):
  """ Run the command up to MAXIMUM_RETRIES times. """
  num_retries = 0
  while num_retries < MAXIMUM_RETRIES:
    ret = run_with_timeout(cmd, TIMEOUT*60)
    if ret == -signal.SIGINT or ret == 0:
      # SIGINT is the one we send and want to fake as success. Anything else
      # than 0 should be an actual build error.
      return 0
    num_retries +=1
    if num_retries < MAXIMUM_RETRIES:
      print "Last build was killed after %d minutes. Starting again..." % TIMEOUT
  return -1

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
  ret = run_build(CMD)
  if ret != 0:
    return ret

  suite = unittest.TestSuite()
  suite.addTests(unittest.TestLoader().loadTestsFromModule(run_cpu_tests))
  suite.addTests(unittest.TestLoader().loadTestsFromModule(run_ruby_tests))
  result = unittest.TextTestRunner(verbosity=2).run(suite)
  retcode = 0 if result.wasSuccessful() else 1
  sys.exit(retcode)

if __name__ == "__main__":
  main()
