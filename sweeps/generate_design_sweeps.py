#!/usr/bin/env python
import argparse
import ConfigParser
import getpass
import math
import os
import sys

from sweep_config import *
from shoc_config import SHOC
from machsuite_config import MACH

GEM5_CFG = "gem5.cfg"
ALADDIN_CFG = "aladdin.cfg"
CACTI_CACHE_CFG = "cacti_cache.cfg"
CACTI_TLB_CFG = "cacti_tlb.cfg"
CACTI_LQ_CFG = "cacti_lq.cfg"
CACTI_SQ_CFG = "cacti_sq.cfg"

# Taken from the template cfg file in <gem5_home>/configs/aladdin.
GEM5_DEFAULTS = {
  "cycle_time": 1,
  "memory_type": "cache",
  "spad_ports": 1,
  "tlb_hit_latency": 0,
  "tlb_miss_latency": 100,
  "tlb_page_size": 4096,
  "tlb_entries": 0,
  "tlb_max_outstanding_walks": 0,
  "tlb_assoc": 0,
  "tlb_bandwidth": 1,
  "is_perfect_tlb": False,
  "load_bandwidth": 1,
  "load_queue_size": 16,
  "store_bandwidth": 1,
  "store_queue_size": 16,
  "cache_assoc": 2,
  "cache_hit_latency": 1,
  "cache_line_sz" : 64,
  "dma_setup_latency" : 1,
  "max_dma_requests" : 16,
}

def generate_aladdin_config(benchmark, config_name, params):
  """ Write an Aladdin configuration file for the specified parameters.

  Args:
    benchmark: A benchmark description object.
    config_name: Name of the configuration.
    params: Configuration parameters. Must include the keys partition,
        unrolling, and pipelining.
  """
  os.chdir(config_name)
  config_file = open(ALADDIN_CFG, "wb")
  if "pipelining" in params:
    config_file.write("pipelining,%d\n" % params["pipelining"])

  if "partition" in params:
    for array in benchmark.arrays:
      if array.partition_type == PARTITION_CYCLIC:
        config_file.write("partition,cyclic,%s,%d,%d,%d\n" %
                          (array.name,
                           array.size*array.word_size,
                           array.word_size,
                           params["partition"]))
      elif array.partition_type == PARTITION_BLOCK:
        config_file.write("partition,block,%s,%d,%d,%d\n" %
                          (array.name,
                           array.size*array.word_size,
                           array.word_size,
                           params["partition"]))
      elif array.partition_type == PARTITION_COMPLETE:
        config_file.write("partition,complete,%s,%d\n" %
                          (array.name, array.size*array.word_size))
      else:
        print("Invalid array partitioning configuration for array %s." %
              array.name)
        exit(1)
  # md-grid specific unrolling config
  if benchmark.name == "md-grid":
    config_file.write("unrolling,md,46,1\n")
    config_file.write("unrolling,md,47,1\n")
    config_file.write("unrolling,md,48,1\n")
    config_file.write("unrolling,md,50,1\n")
    config_file.write("unrolling,md,51,1\n")
    config_file.write("unrolling,md,52,1\n")
    if params["unrolling"] <= 4:
      config_file.write("unrolling,md,56,1\n")
      config_file.write("unrolling,md,62,%d\n" %(params["unrolling"]))
    elif params["unrolling"] <=16:
      config_file.write("unrolling,md,56,%d\n" %(params["unrolling"]/4))
      config_file.write("flatten,md,62\n")
  # md-knn specific unrolling config
  elif benchmark.name == "md-knn":
    if params["unrolling"] <= 16:
      config_file.write("unrolling,md,51,1\n")
      config_file.write("unrolling,md,58,%d\n" %(params["unrolling"]))
    else:
      config_file.write("unrolling,md,51,%d\n" %(params["unrolling"]/16))
      config_file.write("flatten,md,58\n")
  else:
    for loop in benchmark.loops:
      if (loop.trip_count == UNROLL_FLATTEN):
        config_file.write("flatten,%s,%d\n" % (loop.name, loop.line_num))
      elif (loop.trip_count == UNROLL_ONE):
        config_file.write("unrolling,%s,%d,%d\n" %
                          (loop.name, loop.line_num, loop.trip_count))
      else:
        config_file.write("unrolling,%s,%d,%d\n" %
                          (loop.name, loop.line_num, params["unrolling"]))

  config_file.close()
  os.chdir("..")

def write_cacti_config(config_file, config_name, params):
  """ Writes CACTI 6.5+ config files to the provided file handle. """
  config_file.write("// Config: %s\n" % config_name)  # File comment
  cache_size = params["cache_size"]
  if cache_size < 64:
    cache_size = 64
  config_file.write("-size (bytes) %d\n" % cache_size)
  config_file.write("-associativity %d\n" % params["cache_assoc"])
  config_file.write("-read-write port %d\n" % params["rw_ports"])
  config_file.write("-cache type \"%s\"\n" % params["cache_type"])
  config_file.write("-block size (bytes) %d\n" % params["line_size"])
  config_file.write("-search port %d\n" % params["search_ports"])
  config_file.write("-output/input bus width %d\n" % params["io_bus_width"])
  config_file.write("-exclusive write port %d\n" % params["exw_ports"])
  config_file.write("-UCA bank count %d\n" % params["banks"])
  # Default parameters
  config_file.write(
      "-Power Gating - \"false\"\n"
      "-Power Gating Performance Loss 0.01\n"
      "-exclusive read port 0\n"
      "-single ended read ports 0\n"
      "-technology (u) 0.040\n"
      "-page size (bits) 8192 \n"
      "-burst length 8\n"
      "-internal prefetch width 8\n"
      "-Data array cell type - \"itrs-hp\"\n"
      "-Tag array cell type - \"itrs-hp\"\n"
      "-Tag array peripheral type - \"itrs-hp\"\n"
      "-hp Vdd (V) \"default\"\n"
      "-lstp Vdd (V) \"default\"\n"
      "-lop Vdd (V) \"default\"\n"
      "-Long channel devices - \"true\"\n"
      "-operating temperature (K) 300\n"
      "-tag size (b) \"default\"\n"
      "-access mode (normal, sequential, fast) - \"normal\"\n"
      "-design objective (weight delay, dynamic power, leakage power, "
          "cycle time, area) 0:0:0:100:0\n"
      "-deviate (delay, dynamic power, leakage power, cycle time, area) "
          "20:100000:100000:100000:100000\n"
      "-NUCAdesign objective (weight delay, dynamic power, leakage power, "
          "cycle time, area) 100:100:0:0:100\n"
      "-NUCAdeviate (delay, dynamic power, leakage power, cycle time, area) "
          "10:10000:10000:10000:10000\n"
      "-Optimize ED or ED^2 (ED, ED^2, NONE): \"NONE\"\n"
      "-Cache model (NUCA, UCA)  - \"UCA\"\n"
      "-NUCA bank count 0\n"
      "-Wire signalling (fullswing, lowswing, default) - \"Global_30\"\n"
      "-Wire inside mat - \"semi-global\"\n"
      "-Wire outside mat - \"semi-global\"\n"
      "-Interconnect projection - \"conservative\"\n"
      "-Core count 1\n"
      "-Cache level (L2/L3) - \"L2\"\n"
      "-Add ECC - \"true\"\n"
      "-Print level (DETAILED, CONCISE) - \"DETAILED\"\n"
      "-Print input parameters - \"true\"\n"
      "-Force cache config - \"false\"\n"
      "-Ndwl 1\n"
      "-Ndbl 1\n"
      "-Nspd 0\n"
      "-Ndcm 1\n"
      "-Ndsam1 0\n"
      "-Ndsam2 0\n")

def generate_all_cacti_configs(benchmark_name, config_name, params):
  os.chdir(config_name)
  config_file = open(CACTI_CACHE_CFG, "wb")
  if "cache_assoc" in params:
    cache_assoc = params["cache_assoc"]
  else:
    cache_assoc = GEM5_DEFAULTS["cache_assoc"]
  if "cache_line_sz" in params:
    cache_line_sz = params["cache_line_sz"]
  else:
    cache_line_sz = GEM5_DEFAULTS["cache_line_sz"]
  cache_params = {"cache_size": params["cache_size"],
                  "cache_assoc": cache_assoc,
                  "rw_ports": 0,
                  "exw_ports": 2, # Two exclusive write ports
                  "exr_ports": 2, # Two exclusive read ports
                  "line_size": cache_line_sz,
                  "banks" : 2, # Two banks
                  "cache_type": "cache",
                  "search_ports": 0,
                  # Note for future reference - this may have to change per
                  # benchmark.
                  "io_bus_width" : cache_line_sz * 8}
  write_cacti_config(config_file, config_name, cache_params)
  config_file.close()

  config_file = open(CACTI_TLB_CFG, "wb")
  tlb_params = {"cache_size": params["tlb_entries"]*4,
                "cache_assoc": 0,  # fully associative
                "rw_ports": 0,
                "exw_ports":  1,  # One write port for miss returns.
                "line_size": 4,  # 32b per TLB entry. in bytes
                "banks" : 1,
                "cache_type": "ram",
                "search_ports": max(params["load_bandwidth"],
                                    params["store_bandwidth"]),
                "io_bus_width" : 32}
  write_cacti_config(config_file, config_name, tlb_params)
  config_file.close()

  config_file = open(CACTI_LQ_CFG, "wb")
  lq_params = {"cache_size": params["load_queue_size"]*4,
               "cache_assoc": 0,  # fully associative
               "rw_ports": 0,
               "exw_ports":  1,
               "banks" : 1,
               "line_size": 4,  # 32b per entry
               "cache_type": "ram",
               "search_ports": params["load_bandwidth"],
               "io_bus_width" : 32}
  write_cacti_config(config_file, config_name, lq_params)
  config_file.close()

  config_file = open(CACTI_SQ_CFG, "wb")
  sq_params = {"cache_size": params["store_queue_size"]*4,
               "cache_assoc": 0,  # fully associative
               "rw_ports": 0,
               "exw_ports":  1,
               "banks" : 1,
               "line_size": 4,  # 32b per entry
               "cache_type": "ram",
               "search_ports": params["store_bandwidth"],
               "io_bus_width" : 32}
  write_cacti_config(config_file, config_name, sq_params)
  config_file.close()

  os.chdir("..")

def generate_gem5_config(benchmark_name, config_name, params):
  """ Writes a GEM5 config file for Aladdin inside the config directory.

  Args:
    benchmark_name: Name of the benchmark.
    config_name: Name of the configuration.
    params: A dict of parameters. The keys must include the keys used in the
        GEM5 config, but only those that are in GEM5_DEFAULTS will be written to
        the config file.
  """
  config = ConfigParser.SafeConfigParser(GEM5_DEFAULTS)
  config.add_section(benchmark_name)

  # Create an output directory for Aladdin to dump temporary files.
  benchmark_top_dir = os.getcwd()
  os.chdir(config_name)
  cur_config_dir = "%s/%s" % (benchmark_top_dir, config_name)
  output_dir = "%s/outputs" % (cur_config_dir)
  if not os.path.exists(output_dir):
    os.makedirs(output_dir)

  config.set(benchmark_name, "input_dir", cur_config_dir)
  config.set(benchmark_name, "bench_name",
             "%%(input_dir)s/outputs/%s" % benchmark_name)
  config.set(benchmark_name, "trace_file_name",
             "%s/inputs/dynamic_trace" % benchmark_top_dir)
  config.set(benchmark_name, "config_file_name",
             "%%(input_dir)s/%s" % ALADDIN_CFG)
  if params["memory_type"] == "cache":
    # Cache size in GEM5 is specified like 64kB, rather than 65536.
    cache_size_str = "%dkB" % (int(params["cache_size"])/1024)
    config.set(benchmark_name, "cache_size", cache_size_str)
    config.set(benchmark_name, "cacti_cache_config",
               "%%(input_dir)s/%s" % (CACTI_CACHE_CFG))
    config.set(benchmark_name, "cacti_tlb_config",
               "%%(input_dir)s/%s" % (CACTI_TLB_CFG))
    config.set(benchmark_name, "cacti_lq_config",
               "%%(input_dir)s/%s" % (CACTI_LQ_CFG))
    config.set(benchmark_name, "cacti_sq_config",
               "%%(input_dir)s/%s" % (CACTI_SQ_CFG))

    # Store queue is usually half of the size of load queue.
    params["store_bandwidth"] = params["load_bandwidth"]/2
    params["store_queue_size"] = params["load_queue_size"]/2
    # Set max number of tlb outstanding walks the same as TLB sizes
    params["tlb_max_outstanding_walks"] = params["tlb_entries"]
    # Set TLB bandwidth the same as max(load/store_queue_bw)
    params["tlb_bandwidth"] = min(params["load_bandwidth"],
                                  params["tlb_entries"])
  for key in GEM5_DEFAULTS.iterkeys():
    if key in params:
      config.set(benchmark_name, key, str(params[key]))

  with open(GEM5_CFG, "wb") as configfile:
    config.write(configfile)
  os.chdir("..")

def generate_configs_recurse(benchmark, set_params, sweep_params):
  """ Recursively generate all possible configuration settings.

  On each iteration, this function pops a SweepParam object from sweep_params,
  generates all possible values of this parameter, and for each of these values,
  adds an entry into set_params. On each iteration, it will recursively call
  this function until all sweep parameters have been populated with values in
  set_params. Then, it will write the configuration files accordingly.

  Args:
    benchmark: A benchmark description object.
    set_params: Parameters in the sweep that have been assigned a value.
    sweep_params: Parameters in the sweep that have not yet been assigned a
      value.
  """
  if len(sweep_params) > 0:
    local_sweep_params = [i for i in sweep_params]  # Local copy.
    next_param = local_sweep_params.pop()
    value_range = []
    # Generate all values of this parameter as set by the sweep start, end, and
    # step. If the parameter was set to NO_SWEEP, then we just use the start
    # value.
    if next_param.step == NO_SWEEP:
      value_range = [next_param.start]
    else:
      if next_param.step_type == LINEAR_SWEEP:
        value_range = range(next_param.start, next_param.end+1, next_param.step)
      elif next_param.step_type == EXP_SWEEP:
        value_range = [next_param.start * (next_param.step ** exp)
                       for exp in range(0,
                           int(math.log(next_param.end/next_param.start,
                                        next_param.step))+1)]
    for value in value_range:
      set_params[next_param.name] = value
      generate_configs_recurse(benchmark, set_params, local_sweep_params)
  else:
    # All parameters have been populated with values. We can write the
    # configuration now.

    # The config will use load_bandwidth as "bandwidth" in the naming since it's
    # probable that even memory-bound accelerators are read-bound and not
    # write-bound.
    if set_params["memory_type"] == "spad" or set_params["memory_type"] == "dma":
      CONFIG_NAME_FORMAT = "pipe%d_unr_%d_part_%d"
      config_name = CONFIG_NAME_FORMAT % (set_params["pipelining"],
                                          set_params["unrolling"],
                                          set_params["partition"])
    elif set_params["memory_type"] == "cache":
      if set_params["load_bandwidth"] > set_params["load_queue_size"]:
        return
      CONFIG_NAME_FORMAT = "pipe%d_unr_%d_tlb_%d_ldbw_%d_ldq_%d_size_%d"
      config_name = CONFIG_NAME_FORMAT % (set_params["pipelining"],
                                          set_params["unrolling"],
                                          set_params["tlb_entries"],
                                          set_params["load_bandwidth"],
                                          set_params["load_queue_size"],
                                          set_params["cache_size"])
    if benchmark.name == "md-grid" and set_params["unrolling"] > 16:
      return
    print "  Configuration %s" % config_name
    if not os.path.exists(config_name):
      os.makedirs(config_name)
    generate_aladdin_config(benchmark, config_name, set_params)
    generate_gem5_config(benchmark.name, config_name, set_params)
    if set_params["memory_type"] == "cache":
      generate_all_cacti_configs(benchmark.name, config_name, set_params)

def generate_all_configs(benchmark, memory_type):
  """ Generates all the possible configurations for the design sweep. """
  if memory_type == "spad" or memory_type == "dma":
    all_sweep_params = [pipelining,
                        unrolling,
                        partition]
  elif memory_type == "cache":
    all_sweep_params = [pipelining,
                        unrolling,
                        tlb_entries,
                        load_bandwidth,
                        load_queue_size,
                        cache_size]
  # This dict stores a single configuration.
  params = {"memory_type": memory_type}
  # Recursively generate all possible configurations.
  generate_configs_recurse(benchmark, params, all_sweep_params)

def write_config_files(benchmark, output_dir, memory_type):
  """ Create the directory structure and config files for a benchmark. """
  # This assumes we're already in output_dir.
  if not os.path.exists(benchmark.name):
    os.makedirs(benchmark.name)
  print "Generating configurations for %s" % benchmark.name
  os.chdir(benchmark.name)
  generate_all_configs(benchmark, memory_type)
  os.chdir("..")

def run_sweeps(workload, output_dir, dry_run=False):
  """ Run the design sweep on the given workloads.

  This function will also write a convenience Bash script to the configuration
  directory so a user can manually run a single simulation directly.  During a
  dry run, these scripts will still be written, but the simulations themselves
  will not be executed.

  Args:
    workload: List of benchmark description objects.
    output_dir: Top-level directory of the configuration files.
    dry_run: True for a dry run.
  """
  cwd = os.getcwd()
  gem5_home = cwd.split("sweeps")[0]
  print gem5_home
  # Turning on debug outputs for CacheDatapath can incur a huge amount of disk
  # space, most of which is redundant, so we leave that out of the command here.
  run_cmd = ("%s/build/X86/gem5.opt --debug-file=%s/debug.out "
             "%s/configs/aladdin/aladdin_se.py --num-cpus=0 --mem-size=2GB "
             "--sys-clock=1GHz "
             "--mem-type=ddr3_1600_x64 "
             "--cpu-type=timing --caches "
             #"--cpu-type=timing --caches --l2cache --is_perfect_l2_cache=0 "
             #"--cpu-type=timing --caches --l2cache --is_perfect_l2_cache=1 "
             #"--is_perfect_l2_bus=1 --mem-latency=0ns "
             "--aladdin_cfg_file=%s > %s/stdout 2> %s/stderr")
  os.chdir("..")
  for benchmark in workload:
    print "------------------------------------"
    print "Executing benchmark %s" % benchmark.name
    bmk_dir = "%s/%s/%s" % (cwd, output_dir, benchmark.name)
    configs = [file for file in os.listdir(bmk_dir)
               if os.path.isdir("%s/%s" % (bmk_dir, file))]
    for config in configs:
      abs_cfg_path = "%s/%s/%s" % (bmk_dir, config, GEM5_CFG)
      if not os.path.exists(abs_cfg_path):
        continue
      abs_output_path = "%s/%s/outputs" % (bmk_dir, config)
      cmd = run_cmd % (gem5_home, abs_output_path, gem5_home, abs_cfg_path,
                       abs_output_path, abs_output_path)
      # Create a run.sh convenience script in this directory so that we can
      # quickly run a single config.
      run_script = open("%s/%s/run.sh" % (bmk_dir, config), "wb")
      run_script.write("#!/usr/bin/env bash\n"
                       "%s\n" % cmd)
      run_script.close()
      print "     %s" % config
      if not dry_run:
        os.system(cmd)
  os.chdir(cwd)

def generate_traces(workload, output_dir, source_dir, memory_type):
  """ Generates dynamic traces for each workload.

  The traces are placed into <output_dir>/<benchmark>/inputs. This compilation
  procedure is very simple and assumes that all the relevant code is contained
  inside a single source file, with the exception that a separate test harness
  can be specified through the Benchmark description objects.

  Args:
    workload: A list of Benchmark description objects.
    output_dir: The primary configuration output directory.
    source_dir: The top-level directory of the benchmark suite.
  """
  if not "TRACER_HOME" in os.environ:
    raise Exception("Set TRACER_HOME directory as an environment variable")
  trace_dir = "inputs"
  cwd = os.getcwd()
  for benchmark in workload:
    print benchmark.name
    os.chdir("%s/%s" % (output_dir, benchmark.name))
    if not os.path.exists(trace_dir):
      os.makedirs(trace_dir)
    os.chdir(trace_dir)
    if workload == SHOC:
      source_file_prefix = "%s/%s/%s" % (source_dir, benchmark.name, benchmark.source_file)
    elif workload == MACH:
      source_file_prefix = "%s/%s/%s/%s" % (source_dir, \
        benchmark.name.split('-')[0], benchmark.name.split('-')[1], benchmark.source_file)
    output_file_prefix = ("%s/%s/%s/inputs/%s" %
                          (cwd, output_dir, benchmark.name, benchmark.source_file))
    source_file = source_file_prefix + ".c"
    obj = output_file_prefix + ".llvm"
    opt_obj = output_file_prefix + "-opt.llvm"
    test_file = "%s/%s" % ( source_dir, benchmark.test_harness)
    test_obj = output_file_prefix + "_test.llvm"
    full_llvm = output_file_prefix + "_full.llvm"
    full_s = output_file_prefix + "_full.s"
    executable = output_file_prefix + "-instrumented"
    os.environ["WORKLOAD"]=",".join(benchmark.kernels)
    all_objs = [opt_obj]

    defines = " "
    if memory_type == "dma":
      defines += "-DDMA_MODE "
    # Compile the source file.
    os.system("clang -g -O1 -S -fno-slp-vectorize -fno-vectorize "
              "-I" + os.environ["ALADDIN_HOME"] + " " + defines +
              "-fno-unroll-loops -fno-inline -emit-llvm -o " + obj +
              " "  + source_file)
    # Compile the test harness if applicable.
    if benchmark.test_harness:
      all_objs.append(test_obj)
      os.system("clang -g -O1 -S -fno-slp-vectorize -fno-vectorize "
                "-fno-unroll-loops -fno-inline -emit-llvm -o " + test_obj +
                " "  + test_file)

    # Finish compilation, linking, and then execute the instrumented code to get
    # the dynamic trace.
    os.system("opt -S -load=" + os.getenv("TRACER_HOME") +
              "/full-trace/full_trace.so -fulltrace " + obj + " -o " + opt_obj)
    os.system("llvm-link -o " + full_llvm + " " + " ".join(all_objs) + " " +
              os.getenv("TRACER_HOME") + "/profile-func/trace_logger.llvm")
    os.system("llc -O0 -disable-fp-elim -filetype=asm -o " + full_s + " " + full_llvm)
    os.system("gcc -O0 -fno-inline -o " + executable + " " + full_s + " -lm")
    # Change directory so that the dynamic_trace file gets put in the right
    # place.
    os.chdir("%s/%s/%s/inputs" % (cwd, output_dir, benchmark.name))
    if workload == SHOC:
      os.system(executable)
    elif workload == MACH:
      os.system(executable + " %s/%s/%s/input.data %s/%s/%s/check.data" % \
       (source_dir, benchmark.name.split('-')[0], benchmark.name.split('-')[1],\
       source_dir, benchmark.name.split('-')[0], benchmark.name.split('-')[1]))
    os.chdir(cwd)

def generate_condor_scripts(workload, output_dir, username):
  """ Generate a single Condor script for the complete design sweep. """
  # First, generate all the runscripts.
  run_sweeps(workload, output_dir, dry_run=True)
  cwd = os.getcwd()
  os.chdir("..")
  basicCondor = [
      "#Submit this file in the directory where the input and output files live",
      "Universe        = vanilla",
      "# With the following setting, Condor will try to copy and use the environ-",
      "# ment of the submitter, provided its not too large.",
      "GetEnv          = True",
      "# This forces the jobs to be run on the less crowded server",
      "Requirements    = (OpSys == \"LINUX\") && (Arch == \"X86_64\") && ( TotalMemory > 128000)",
      "# Change the email address to suit yourself",
      "Notify_User     = %s@eecs.harvard.edu" % username,
      "Notification    = Error",
      "Executable = /bin/bash"]

  f = open("%s/submit.con" % cwd, "w")
  for b in basicCondor:
    f.write(b + " \n")

  for benchmark in workload:
    bmk_dir = "%s/%s/%s" % (cwd, output_dir, benchmark.name)
    configs = [file for file in os.listdir(bmk_dir)
               if os.path.isdir("%s/%s" % (bmk_dir, file))]
    for config in configs:
      abs_cfg_path = "%s/%s/%s" % (bmk_dir, config, GEM5_CFG)
      if not os.path.exists(abs_cfg_path):
        continue
      f.write("InitialDir = %s/%s/\n" % (bmk_dir, config))
      f.write("Arguments = %s/%s/run.sh\n" % (bmk_dir, config))
      f.write("Queue\n\n")
  f.close()
  os.chdir(cwd)

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument(
      "mode", choices=["configs", "trace", "run", "all", "condor"], help=
      "Run mode. \"configs\" will generate all possible configurations for the "
      "desired sweep. \"trace\" will build dynamic traces for all benchmarks. "
      "\"run\" will run the generated design sweep for a benchmark suite. "
      "\"all\" will do all of the above in that order. \"condor\" will write a "
      "condor job script for the complete design sweep, but this mode is not "
      "included in \"all\".")
  parser.add_argument("--output_dir", required=True, help="Config output "
                      "directory. Required for all modes.")
  parser.add_argument("--memory_type", help="\"cache\" or \"spad\" or \"dma\". "
                      "Required for config mode.")
  parser.add_argument("--benchmark_suite", help="SHOC or MachSuite. Required "
                      "for config and trace modes.")
  parser.add_argument("--source_dir", help="Path to the benchmark suite "
                      "directory. Required for trace mode.")
  parser.add_argument("--dry", action="store_true", help="Perform a dry run. "
      "Simulations will not be executed, but a convenience Bash script will be "
      "written to each config directory so the user can run that config "
      "simulation manually.")
  parser.add_argument("--username", help="Username for the Condor scripts. If "
      "this is not provided, Python will try to figure it out.")
  args = parser.parse_args()

  workload = []
  if args.benchmark_suite == "SHOC":
    workload = SHOC
  elif args.benchmark_suite == "MachSuite":
    workload = MACH

  if args.mode == "configs" or args.mode == "all":
    if (not args.memory_type or
        not args.benchmark_suite):
      print "Missing some required inputs! See help documentation (-h)."
      exit(1)

    current_dir = os.getcwd()
    if not os.path.exists(args.output_dir):
      os.makedirs(args.output_dir)
    os.chdir(args.output_dir)
    for benchmark in workload:
      write_config_files(benchmark, args.output_dir, args.memory_type)
    os.chdir(current_dir)

  if args.mode == "trace" or args.mode == "all":
    if (not args.benchmark_suite):
      print "Missing benchmark_suite parameter! See help documentation (-h)"
      exit(1)

    if not args.source_dir:
      print "Need to specify the benchmark suite source directory!"
      exit(1)
    generate_traces(workload, args.output_dir, args.source_dir, args.memory_type)

  if args.mode == "run" or args.mode == "all":
    if not args.benchmark_suite:
      print "Missing benchmark_suite parameter! See help documentation (-h)"
      exit(1)
    run_sweeps(workload, args.output_dir, args.dry)

  if args.mode == "condor":
    if not args.benchmark_suite:
      print "Missing benchmark_suite parameter! See help documentation (-h)"
      exit(1)

    if not args.username:
      args.username = getpass.getuser()
      print "Username was not specified for Condor. Using %s." % args.username
    generate_condor_scripts(workload, args.output_dir, args.username)

if __name__ == "__main__":
  main()
