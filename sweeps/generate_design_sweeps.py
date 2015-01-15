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
from cortexsuite_config import CORTEXSUITE
from cortexsuite_indep_config import CORTEXSUITE_KERNELS

GEM5_CFG = "gem5.cfg"
ALADDIN_CFG = "aladdin"
CACTI_CACHE_CFG = "cacti_cache"
CACTI_TLB_CFG = "cacti_tlb"
CACTI_LQ_CFG = "cacti_lq"
CACTI_SQ_CFG = "cacti_sq"

# Taken from the template cfg file in <gem5_home>/configs/aladdin.
GEM5_DEFAULTS = {
  "cycle_time": 1,
  "memory_type": "cache",
  "spad_ports": 1,
  "dma_setup_latency" : 100,
  "max_dma_requests" : 16,
}

L1CACHE_DEFAULTS = {
  "cache_size": 16384,
  "cache_assoc": 4,
  "cache_hit_latency": 1,
  "cache_line_sz" : 64,
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
  "store_queue_size": 16
}

L2CACHE_DEFAULTS = {
  "cache_size": 131072,
  "cache_assoc": 8,
  "cache_hit_latency": 6,
  "cache_line_sz": 64
}

def write_aladdin_array_configs(benchmark, config_file, params):
  """ Write the Aladdin array partitioning configurations. """
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

def write_benchmark_specific_configs(benchmark, config_file, params):
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

def generate_aladdin_config(benchmark, kernel, params, loops):
  """ Write an Aladdin configuration file for the specified parameters.

  Args:
    benchmark: A benchmark description object.
    kernel: Either "aladdin" or the name of the individual kernel.
    params: Kernel configuration parameters. Must include the keys partition,
        unrolling, and pipelining.
    loops: The list of loops to include in the config file.
  """
  config_file = open("%s.cfg" % kernel, "wb")
  if "pipelining" in params:
    config_file.write("pipelining,%d\n" % params["pipelining"])
  # TODO: Currently we're not separating arrays by kernel. This needs to
  # change.
  write_aladdin_array_configs(benchmark, config_file, params)
  write_benchmark_specific_configs(benchmark, config_file, params)

  for loop in loops:
    if loop.trip_count == UNROLL_FLATTEN:
      config_file.write("flatten,%s,%d\n" % (loop.name, loop.line_num))
    elif loop.trip_count == UNROLL_ONE:
      config_file.write("unrolling,%s,%d,%d\n" %
                        (loop.name, loop.line_num, loop.trip_count))
    elif (loop.trip_count == ALWAYS_UNROLL or
          params["unrolling"] < loop.trip_count):
      # We only unroll if it was specified to always unroll or if the loop's
      # trip count is greater than the current unrolling factor.
      config_file.write("unrolling,%s,%d,%d\n" %
                        (loop.name, loop.line_num, params["unrolling"]))
    elif params["unrolling"] >= loop.trip_count:
      config_file.write("flatten,%s,%d\n" % (loop.name, loop.line_num))
  config_file.close()

def write_cacti_config(config_file, params):
  """ Writes CACTI 6.5+ config files to the provided file handle. """
  cache_size = max(64, params["cache_size"])
  config_file.write("-size (bytes) %d\n" % cache_size)
  config_file.write("-associativity %d\n" % params["cache_assoc"])
  config_file.write("-read-write port %d\n" % params["rw_ports"])
  config_file.write("-cache type \"%s\"\n" % params["cache_type"])
  config_file.write("-block size (bytes) %d\n" % params["line_size"])
  config_file.write("-search port %d\n" % params["search_ports"])
  config_file.write("-output/input bus width %d\n" % params["io_bus_width"])
  config_file.write("-exclusive write port %d\n" % params["exw_ports"])
  config_file.write("-exclusive read port %d\n" % params["exr_ports"])
  config_file.write("-UCA bank count %d\n" % params["banks"])
  # Default parameters
  config_file.write(
      "-Power Gating - \"false\"\n"
      "-Power Gating Performance Loss 0.01\n"
      "-single ended read ports 0\n"
      "-technology (u) 0.040\n"
      "-page size (bits) 8192 \n"
      "-burst length 8\n"
      "-internal prefetch width 8\n"
      "-Data array cell type - \"itrs-hp\"\n"
      "-Tag array cell type - \"itrs-hp\"\n"
      "-Data array peripheral type - \"itrs-hp\"\n"
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

def generate_all_cacti_configs(benchmark_name, kernel, params):
  config_file = open("%s_%s.cfg" % (kernel, CACTI_CACHE_CFG), "wb")
  if "cache_assoc" in params:
    cache_assoc = params["cache_assoc"]
  else:
    cache_assoc = GEM5_DEFAULTS["cache_assoc"]
  if "cache_line_sz" in params:
    cache_line_sz = params["cache_line_sz"]
  else:
    cache_line_sz = GEM5_DEFAULTS["cache_line_sz"]
  if "cache_size" in params:
    cache_size = params["cache_size"]
  else:
    cache_size = GEM5_DEFAULTS["cache_size"]
  cache_params = {"cache_size": cache_size,
                  "cache_assoc": cache_assoc,
                  "rw_ports": 0,
                  "exw_ports": params["load_bandwidth"], # Two exclusive write ports
                  "exr_ports": params["store_bandwidth"], # Two exclusive read ports
                  "line_size": cache_line_sz,
                  "banks" : 2, # Two banks
                  "cache_type": "cache",
                  "search_ports": 0,
                  # Note for future reference - this may have to change per
                  # benchmark.
                  "io_bus_width" : cache_line_sz * 8}
  write_cacti_config(config_file, cache_params)
  config_file.close()

  config_file = open("%s_%s.cfg" % (kernel, CACTI_TLB_CFG), "wb")
  # Set TLB bandwidth the same as max(load/store_queue_bw)
  params["tlb_bandwidth"] = min(params["load_bandwidth"],
                                params["tlb_entries"])
  tlb_params = {"cache_size": params["tlb_entries"]*4,
                "cache_assoc": 0,  # fully associative
                "rw_ports": 0,
                "exw_ports":  1,  # One write port for miss returns.
                "exr_ports": params["tlb_bandwidth"],
                "line_size": 4,  # 32b per TLB entry. in bytes
                "banks" : 1,
                "cache_type": "cache",
                "search_ports": params["tlb_bandwidth"],
                "io_bus_width" : 32}
  write_cacti_config(config_file, tlb_params)
  config_file.close()

  config_file = open("%s_%s.cfg" % (kernel, CACTI_LQ_CFG), "wb")
  lq_params = {"cache_size": params["load_queue_size"]*4,
               "cache_assoc": 0,  # fully associative
               "rw_ports": params["load_bandwidth"],
               "exw_ports":  0,
               "exr_ports": 0,
               "banks" : 1,
               "line_size": 4,  # 32b per entry
               "cache_type": "cache",
               "search_ports": params["load_bandwidth"],
               "io_bus_width" : 32}
  write_cacti_config(config_file, lq_params)
  config_file.close()

  config_file = open("%s_%s.cfg" % (kernel, CACTI_SQ_CFG), "wb")
  sq_params = {"cache_size": params["store_queue_size"]*4,
               "cache_assoc": 0,  # fully associative
               "rw_ports": params["store_bandwidth"],
               "exw_ports":  0,
               "exr_ports": 0,
               "banks" : 1,
               "line_size": 4,  # 32b per entry
               "cache_type": "cache",
               "search_ports": params["store_bandwidth"],
               "io_bus_width" : 32}
  write_cacti_config(config_file, sq_params)
  config_file.close()

def handle_gem5_cache_config(params):
  """ Converts a power of 2 into the format XXkB, as GEM5 requires. """
  l1cache_size = 0
  l2cache_size = 0
  if "cache_size" in params:
    l1cache_size = params["cache_size"]/1024
  else:
    l1cache_size = GEM5_DEFAULTS["cache_size"]/1024
  if "l2cache_size" in params:
    l2cache_size = params["l2cache_size"]/1024
  return (int(l1cache_size), int(l2cache_size))

def generate_gem5_config(benchmark, kernel, params, write_new=True):
  """ Writes a GEM5 config file for Aladdin inside the config directory.

  Args:
    benchmark: The Benchmark description object.
    kernel: Name of the kernel, or ALADDIN_CFG.
    params: A dict of parameters. The keys must include the keys used in the
        GEM5 config, but only those that are in GEM5_DEFAULTS will be written to
        the config file.
    write_new: True if we are writing a new config file. In this case, a
        defaults section will be added, and any existing files will be
        overwritten.
  """
  defaults = GEM5_DEFAULTS if write_new else []
  config = ConfigParser.SafeConfigParser(defaults)
  config.add_section(kernel)

  # Create an output directory for Aladdin to dump temporary files.
  cur_config_file_dir = os.getcwd()
  benchmark_top_dir = os.path.dirname(cur_config_file_dir)  # One dir level up.
  output_dir = "%s/outputs" % (cur_config_file_dir)
  if not os.path.exists(output_dir):
    os.makedirs(output_dir)

  trace_file = (
      "%s_trace" % kernel if benchmark.separate_kernels else "dynamic_trace")
  config.set(kernel, "memory_type", params["memory_type"])
  config.set(kernel, "input_dir", cur_config_file_dir)
  config.set(kernel, "bench_name",
             "%%(input_dir)s/outputs/%s" % kernel)
  config.set(kernel, "trace_file_name",
             "%s/inputs/%s" % (benchmark_top_dir, trace_file))
  config.set(kernel, "config_file_name",
             "%%(input_dir)s/%s.cfg" % kernel)
  if params["memory_type"] == "cache":
    (l1cache_size, l2cache_size) = handle_gem5_cache_config(params)
    config.set(kernel, "cache_size", "%dkB" % l1cache_size)
    config.set(kernel, "l2cache_size", "%dkB" % l2cache_size)
    config.set(kernel, "cacti_cache_config",
               "%%(input_dir)s/%s_%s.cfg" % (kernel, CACTI_CACHE_CFG))
    config.set(kernel, "cacti_tlb_config",
               "%%(input_dir)s/%s_%s.cfg" % (kernel, CACTI_TLB_CFG))
    config.set(kernel, "cacti_lq_config",
               "%%(input_dir)s/%s_%s.cfg" % (kernel, CACTI_LQ_CFG))
    config.set(kernel, "cacti_sq_config",
               "%%(input_dir)s/%s_%s.cfg" % (kernel, CACTI_SQ_CFG))

    # Store queue is usually half of the size of load queue.
    params["store_bandwidth"] = params["load_bandwidth"]/2
    params["store_queue_size"] = params["load_queue_size"]/2
    # Set max number of tlb outstanding walks the same as TLB sizes
    params["tlb_max_outstanding_walks"] = params["tlb_entries"]
    # Set TLB bandwidth the same as max(load/store_queue_bw)
    params["tlb_bandwidth"] = min(params["load_bandwidth"],
                                  params["tlb_entries"])

    for key, value in L1CACHE_DEFAULTS.iteritems():
      if not config.has_option(kernel, key):
        if key in params:
          config.set(kernel, key, str(params[key]))
        else:
          config.set(kernel, key, str(value))

  for key in GEM5_DEFAULTS.iterkeys():
    if key in params and not config.has_option(kernel, key):
      config.set(kernel, key, str(params[key]))

  # Write the accelerator id and dependencies.
  kernel_id = 0  # By default.
  if benchmark.separate_kernels:
    kernel_id = benchmark.get_kernel_id(kernel)
    if kernel_id == -1:
      print("Something went wrong! "
            "%s was not found in the list of kernels." % kernel)
      exit(1)
  config.set(kernel, "accelerator_id", str(kernel_id))
  if benchmark.enforce_order and kernel_id > 0:
    # Kernels depend on the previous kernel, except for the first.
    config.set(kernel, "accelerator_deps", str(kernel_id - 1))
  else:
    config.set(kernel, "accelerator_deps", "")
  if params["experiment_name"]:
    config.set(kernel, "use_db", "True")
    config.set(kernel, "experiment_name", params["experiment_name"])
  else:
    config.set(kernel, "use_db", "False")
    config.set(kernel, "experiment_name", "NULL")

  mode = "w" if write_new else "a"
  with open(GEM5_CFG, mode) as configfile:
    config.write(configfile)

def generate_configs_recurse(benchmark, set_params, sweep_params,
                             perfect_l1, enable_l2):
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
    if next_param.step_type == NO_SWEEP:
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
      generate_configs_recurse(benchmark, set_params, local_sweep_params,
                               perfect_l1, enable_l2)
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
      if perfect_l1:
        CONFIG_NAME_FORMAT = "pipe%d_unr_%d_tlb_%d_ldbw_%d_ldq_%d"
        config_name = CONFIG_NAME_FORMAT % (set_params["pipelining"],
                                            set_params["unrolling"],
                                            set_params["tlb_entries"],
                                            set_params["load_bandwidth"],
                                            set_params["load_queue_size"])
      else:
        CONFIG_NAME_FORMAT = "pipe%d_unr_%d_tlb_%d_ldbw_%d_ldq_%d_size_%d_line_%d"
        config_name = CONFIG_NAME_FORMAT % (set_params["pipelining"],
                                            set_params["unrolling"],
                                            set_params["tlb_entries"],
                                            set_params["load_bandwidth"],
                                            set_params["load_queue_size"],
                                            set_params["cache_size"],
                                            set_params["cache_line_sz"])
    if benchmark.name == "md-grid" and set_params["unrolling"] > 16:
      return
    print "  Configuration %s" % config_name
    if not os.path.exists(config_name):
      os.makedirs(config_name)

    os.chdir(config_name)
    # This is a dict from kernels to its loops and arrays.  If we are generating
    # separate config files for each kernel, then the key is the name of that
    # kernel's config file, and the values are the Loop description objects.
    # Otherwise, we give it a generic name and include all loops specified.
    kernel_setup = {}
    if benchmark.separate_kernels:
      for kernel in benchmark.kernels:
        kernel_setup[kernel] = [
            loop for loop in benchmark.loops if loop.name == kernel]
    else:
      kernel_setup = {benchmark.name: benchmark.loops}
    new_gem5_config = True
    for kernel, loops in kernel_setup.iteritems():
      generate_aladdin_config(benchmark, kernel, set_params, loops)
      generate_gem5_config(benchmark, kernel, set_params, write_new=new_gem5_config)
      if set_params["memory_type"] == "cache":
        generate_all_cacti_configs(benchmark.name, kernel, set_params)
      new_gem5_config = False
    # L2 cache.
    if enable_l2:
      l2cache_params = dict(set_params)
      l2cache_params["cache_size"] = set_params["l2cache_size"]
      write_cacti_config(benchmark.name, l2cache_params)
    os.chdir("..")

def generate_all_configs(
    benchmark, memory_type, experiment_name, perfect_l1, enable_l2):
  """ Generates all the possible configurations for the design sweep. """
  if memory_type == "spad" or memory_type == "dma":
    all_sweep_params = [pipelining,
                        unrolling,
                        partition]
  elif memory_type == "cache":
    if perfect_l1:
      all_sweep_params = [pipelining,
                          unrolling,
                          tlb_entries,
                          load_bandwidth,
                          cache_assoc,
                          load_queue_size]
    else:
      all_sweep_params = [pipelining,
                          unrolling,
                          tlb_entries,
                          tlb_bandwidth,
                          load_bandwidth,
                          load_queue_size,
                          cache_hit_latency,
                          cache_assoc,
                          cache_line_sz,
                          cache_size]
  # This dict stores a single configuration.
  params = {"memory_type": memory_type, "experiment_name": experiment_name}
  # Recursively generate all possible configurations.
  print benchmark.kernels
  generate_configs_recurse(benchmark, params, all_sweep_params,
                           perfect_l1, enable_l2)

def write_config_files(benchmark, output_dir, memory_type,
                       experiment_name=None, perfect_l1=False,
                       enable_l2=False):
  """ Create the directory structure and config files for a benchmark. """
  # This assumes we're already in output_dir.
  if not os.path.exists(benchmark.name):
    os.makedirs(benchmark.name)
  print "Generating configurations for %s" % benchmark.name
  os.chdir(benchmark.name)
  generate_all_configs(
      benchmark, memory_type, experiment_name, perfect_l1, enable_l2)
  os.chdir("..")

def run_sweeps(workload, simulator, output_dir, dry_run=False, enable_l2=False,
               perfect_l1=False):
  """ Run the design sweep on the given workloads.

  This function will also write a convenience Bash script to the configuration
  directory so a user can manually run a single simulation directly.  During a
  dry run, these scripts will still be written, but the simulations themselves
  will not be executed.

  Args:
    workload: List of benchmark description objects.
    simulator: Specifies the simulator to use, gem5 or aladdin.
    output_dir: Top-level directory of the configuration files.
    dry_run: True for a dry run.
    enable_l2: Use a shared L2 last level cache.
    perfect_l1: Use a perfect L1 cache.
  """
  cwd = os.getcwd()
  gem5_home = cwd.split("sweeps")[0]
  print gem5_home
  # Turning on debug outputs for CacheDatapath can incur a huge amount of disk
  # space, most of which is redundant, so we leave that out of the command here.
  run_cmd = ""
  if simulator == "gem5":
    run_cmd = ("%(gem5_home)s/build/X86/gem5.opt "
               "--outdir=%(output_path)s/%(outdir)s "
               "%(gem5_home)s/configs/aladdin/aladdin_se.py "
               "--num-cpus=0 --mem-size=2GB "
               "%(mem_flag)s "
               "--sys-clock=1GHz "
               "--cpu-type=timing --caches %(l2cache_flag)s "
               "%(perfect_l1_flag)s "
               "--aladdin_cfg_file=%(aladdin_cfg_path)s > "
               "%(output_path)s/stdout 2> %(output_path)s/stderr")
  else:
    run_cmd = ("%(aladdin_home)s/common/aladdin "
               "%(output_path)s/%(benchmark_name)s "
               "%(bmk_dir)s/inputs/dynamic_trace "
               "%(config_path)s/%(benchmark_name)s.cfg")
  os.chdir("..")
  # TODO: Since the L2 cache is such an important flag, I'm hardcoding in a few
  # parameters specific to it so we can quickly run experiments with and without
  # it. We should reimplement this later so it's more general.
  l2cache_flag = "--l2cache" if enable_l2 else ""
  mem_flag = "--mem-latency=0ns --mem-type=simple_mem " if perfect_l1 else "--mem-type=ddr3_1600_x64 "
  perfect_l1_flag = "--is_perfect_cache=1 --is_perfect_bus=1 " if perfect_l1 else ""
  if enable_l2:
    file_name = "run_L2.sh"
    outdir = "with_L2"
  elif perfect_l1:
    file_name = "run_perfect_l1.sh"
    outdir = "perfect_l1"
  else:
    outdir = "no_L2"
    file_name = "run.sh"
  for benchmark in workload:
    print "------------------------------------"
    print "Executing benchmark %s" % benchmark.name
    bmk_dir = "%s/%s/%s" % (cwd, output_dir, benchmark.name)
    configs = [file for file in os.listdir(bmk_dir)
               if os.path.isdir("%s/%s" % (bmk_dir, file))]
    for config in configs:
      config_path = "%s/%s" % (bmk_dir, config)
      abs_cfg_path = "%s/%s/%s" % (bmk_dir, config, GEM5_CFG)
      if not os.path.exists(abs_cfg_path):
        continue
      abs_output_path = "%s/%s/outputs" % (bmk_dir, config)
      cmd = ""
      if simulator == "gem5":
        cmd = run_cmd % {"gem5_home": gem5_home,
                         "output_path": abs_output_path,
                         "aladdin_cfg_path": abs_cfg_path,
                         "outdir": outdir,
                         "l2cache_flag": l2cache_flag,
                         "mem_flag": mem_flag,
                         "perfect_l1_flag" : perfect_l1_flag}
      else:
        cmd = run_cmd % {"aladdin_home": os.environ["ALADDIN_HOME"],
                         "benchmark_name": benchmark.name,
                         "output_path": abs_output_path,
                         "bmk_dir": bmk_dir,
                         "config_path": config_path}
      # Create a run.sh convenience script in this directory so that we can
      # quickly run a single config.
      run_script = open("%s/%s/%s" % (bmk_dir, config, file_name), "wb")
      run_script.write("#!/usr/bin/env bash\n"
                       "%s\n" % cmd)
      run_script.close()
      print "     %s" % config
      if not dry_run:
        os.system(cmd)
  os.chdir(cwd)

def handle_local_makefile(benchmark, output_dir, source_dir):
  """ Invoke a benchmark-local Makefile for instrumentation.

  There are five requirements:

    1. Define the environment variable TARGET_DIR to specify the final
       destination of the traces to the Makefile.
    2. Define the environment variable WORKLOAD to specify the kernels being
       traced.
    3. Define the environment variable ACCEL_NAME to specify the name of the
       benchmark. This determines into which subfolder the trace is ultimately
       placed.
    3. The Makefile must have a target called "autotrace" which will compile the
       traces and copy them to TARGET_DIR. It should fail if TARGET_DIR is not
       defined. Depending on the Makefile, the complete dynamic trace may or may
       not be automatically split up into smaller traces for each function or
       loop iteration.
    4. If the generated trace needs to be divided into separate traces, set the
       environment variable SPLIT_TRACE to 1. In separated trace mode, the names
       of the divided traces must be of the format <kernel>_trace. If the
       benchmark or kernel is being simulated on its own, then the trace file
       should be named "dynamic_trace".
  """
  # Set the appropriate environment variable and run the Makefile.
  cwd = os.getcwd()
  source_file_loc = "%s/%s" % (source_dir, benchmark.source_file)
  print "Source: %s" % source_file_loc
  # If traces are being placed, then put them into separate kernel directories.
  # Otherwise, put the trace under the benchmark.name directory.
  trace_subdir = benchmark.name
  trace_abs_dir = os.path.abspath("%s/%s/inputs" % (output_dir, trace_subdir))
  print "trace directory : %s" % trace_abs_dir
  if not os.path.isdir(trace_abs_dir):
    os.makedirs(trace_abs_dir)
  print "trace abs directory : %s" % trace_abs_dir
  print ",".join(benchmark.kernels)
  os.environ["TARGET_DIR"] = trace_abs_dir
  os.environ["ACCEL_NAME"] = benchmark.name
  os.environ["WORKLOAD"] = ",".join(benchmark.kernels)
  if benchmark.separate_kernels:
    os.environ["SPLIT_TRACE"] = "1"
  os.chdir(os.path.dirname(source_file_loc))
  os.system("make autotrace")
  os.chdir(cwd)
  return

def generate_traces(workload, output_dir, source_dir, memory_type):
  """ Generates dynamic traces for each workload.

  If the workloads do not have local Makefiles and were not configured as such,
  then the traces are placed into <output_dir>/<benchmark>/inputs. This
  compilation procedure is very simple and assumes that all the relevant code is
  contained inside a single source file, with the exception that a separate test
  harness can be specified through the Benchmark description objects.

  If the workloads do have local Makefiles and were configured as such in the
  benchmark config script, then that local Makefile is invoked. See
  handle_local_makefile() for more details.

  Args:
    workload: A list of Benchmark description objects.
    output_dir: The primary configuration output directory.
    source_dir: The top-level directory of the benchmark suite.
  """
  if not "TRACER_HOME" in os.environ:
    raise Exception("Set TRACER_HOME directory as an environment variable")
  cwd = os.getcwd()
  trace_dir = "inputs"
  for benchmark in workload:
    if benchmark.makefile:
      handle_local_makefile(benchmark, output_dir, source_dir)
    else:
      trace_output_dir = "%s/%s/%s/%s" % (
          cwd, output_dir, benchmark.name, trace_dir)
      if not os.path.exists(trace_output_dir):
        os.makedirs(trace_output_dir)
      print benchmark.name
      os.chdir(trace_output_dir)
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

      # Finish compilation, linking, and then execute the instrumented code to
      # get the dynamic trace.
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

def generate_condor_scripts(workload, simulator, output_dir, username,
                            enable_l2=False, perfect_l1=False):
  """ Generate a single Condor script for the complete design sweep. """
  # First, generate all the runscripts.
  run_sweeps(workload, simulator, output_dir, True, enable_l2, perfect_l1)
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
  if enable_l2:
    condor_file_name = "submit_L2.con"
    run_file = "run_L2.sh"
  elif perfect_l1:
    condor_file_name = "submit_perfect_l1.con"
    run_file = "run_perfect_l1.sh"
  else:
    condor_file_name = "submit.con"
    run_file = "run.sh"
  f = open("%s/%s" % (cwd, condor_file_name), "w")
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
      f.write("Arguments = %s/%s/%s\n" % (bmk_dir, config, run_file))
      f.write("Queue\n\n")
  f.close()
  os.chdir(cwd)

def main():
  parser = argparse.ArgumentParser(
      description="Generate and run Aladdin design sweeps.",
      formatter_class=argparse.ArgumentDefaultsHelpFormatter)
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
  parser.add_argument("--benchmark_suite", required=True, help="SHOC, "
      "MachSuite, CortexSuite, or CortexSuiteKernels. Required for all modes.")
  parser.add_argument("--source_dir", help="Path to the benchmark suite "
                      "directory. Required for trace mode.")
  parser.add_argument("--dry", action="store_true", help="Perform a dry run. "
      "Simulations will not be executed, but a convenience Bash script will be "
      "written to each config directory so the user can run that config "
      "simulation manually.")
  parser.add_argument("--experiment_name", help="Store the final Aladdin "
      "summary data into a database under this experiment name, which is an "
      "identifier for a set of related simulations.")
  parser.add_argument("--username", help="Username for the Condor scripts. If "
      "this is not provided, Python will try to figure it out.")
  parser.add_argument("--enable_l2", action="store_true", help="Enable the L2 "
      "cache during simulations if applicable. This will cause the simulation "
      "output to be dumped to a directory called outputs/with_L2. Without this "
      "flag, GEM5 output is stored to outputs/no_L2. If generating Condor "
      "scripts, this flag will run L2-enabled simulations.")
  parser.add_argument("--perfect_l1", action="store_true", help="Enable the "
      "perfect l1 cache during simulations if applicable. This will cause the "
      "simulation output to be dumped to a directory called outputs/perfect_l1."
      "Without this flag, GEM5 output is stored to outputs/no_L2. If "
      "generating Condor scripts, this flag will run perfect l1 simulations.")
  parser.add_argument("--simulator", choices=["gem5", "aladdin"],
      default="aladdin", help="Select the simulator to use, gem5 or aladdin.")
  args = parser.parse_args()

  workload = []
  if args.benchmark_suite.upper() == "SHOC":
    workload = SHOC
  elif args.benchmark_suite.upper() == "MACHSUITE":
    workload = MACH
  elif args.benchmark_suite.upper() == "CORTEXSUITE":
    workload = CORTEXSUITE
  elif args.benchmark_suite.upper() == "CORTEXSUITEKERNELS":
    workload = CORTEXSUITE_KERNELS
  else:
    print "Invalid benchmark provided!"
    exit(1)

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
      write_config_files(benchmark, args.output_dir, args.memory_type,
                         experiment_name=args.experiment_name,
                         perfect_l1=args.perfect_l1, enable_l2=args.enable_l2)
    os.chdir(current_dir)

  if args.mode == "trace" or args.mode == "all":
    if (not args.benchmark_suite):
      print "Missing benchmark_suite parameter! See help documentation (-h)"
      exit(1)

    if not args.source_dir:
      print "Need to specify the benchmark suite source directory!"
      exit(1)
    generate_traces(
        workload, args.output_dir, args.source_dir, args.memory_type)

  if args.mode == "run" or args.mode == "all":
    if not args.benchmark_suite:
      print "Missing benchmark_suite parameter! See help documentation (-h)"
      exit(1)
    run_sweeps(
        workload, args.simulator, args.output_dir, dry_run=args.dry,
        enable_l2=args.enable_l2, perfect_l1=args.perfect_l1)

  if args.mode == "condor":
    if not args.benchmark_suite:
      print "Missing benchmark_suite parameter! See help documentation (-h)"
      exit(1)

    if not args.username:
      args.username = getpass.getuser()
      print "Username was not specified for Condor. Using %s." % args.username
    generate_condor_scripts(
        workload, args.simulator, args.output_dir, args.username,
        enable_l2=args.enable_l2, perfect_l1=args.perfect_l1)

if __name__ == "__main__":
  main()
