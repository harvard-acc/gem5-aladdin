#!/usr/bin/env python
import argparse
import ConfigParser
import getpass
import math
import os
import shutil
import sys
import subprocess

from benchmark_configs import benchmarks

try:
  from sweep_config import *
except ImportError:
  sys.exit("ERROR: Missing sweep_config.py.\n"
           "You must define a sweep_config.py script. Please look at "
           "sweep_config_example.py for an example and further instructions.")

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
  "max_dma_requests" : 16,
  "dma_setup_latency" : 40,
  "dma_multi_channel" : False,
  "dma_chunk_size" : 64,
  "issue_dma_ops_asap": False,
  "ignore_cache_flush": False,
  "ready_mode": False,
}

L1CACHE_DEFAULTS = {
  "cache_size": 16384,
  "cache_assoc": 4,
  "cache_hit_latency": 1,
  "cache_line_sz" : 32,
  "tlb_hit_latency": 0,
  "tlb_miss_latency": 20,
  "tlb_page_size": 4096,
  "tlb_entries": 0,
  "tlb_max_outstanding_walks": 0,
  "tlb_assoc": 0,
  "tlb_bandwidth": 1,
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

DYNAMIC_TRACE = "dynamic_trace.gz"

def generate_smart_config_name(set_params):
  """ Generate a config name that only contains parameters which were swept.

  Any parameters that were fixed will not be included in the name. This reduces
  the length of the configuration name.
  """
  name = ""
  for key, value in set_params.iteritems():
    if key == "memory_type" or key == "experiment_name":
      continue
    try:
      parameter = globals()[key]
    except KeyError:
      continue
    if not parameter.step_type == NO_SWEEP:
      name = name + "%s_%d_" % (parameter.short_name, value)
  return name[:-1]  # Drop the trailing underscore

def write_aladdin_array_configs(benchmark, config_file, params):
  """ Write the Aladdin array partitioning configurations.

  Some complexities arise with hybrid memory mode, so here are the rules:

  1. If the memory_type is SPAD, then all arrays will be specified to be
     partitioned (with factor 1 if it was not specified in the sweep config).
  2. If the memory_type is CACHE, then all arrays will be written as cached,
     and partition factors will be disregarded if provided.
  3. If the memory_type is SPAD | CACHE (aka HYBRID), then arrays labeled as
     "spad" will be written as partitioned, and all others will be written as
     cached.
  """
  sweep_memory_type = params["memory_type"]
  # Always set array configs.
  for array in benchmark.arrays:
    if (sweep_memory_type & SPAD and
        (array.memory_type == SPAD or not (sweep_memory_type & CACHE))):
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
    elif sweep_memory_type & CACHE:
      # In the cache mode, if an array is not completely partitioned, access
      # it with cache.
      if array.partition_type == PARTITION_COMPLETE:
        config_file.write("partition,complete,%s,%d\n" %
                          (array.name, array.size*array.word_size))
      elif array.name == "sbox" or array.name == "queue" :
        config_file.write("partition,cyclic,%s,%d,%d,%d\n" %
                          (array.name,
                           array.size*array.word_size,
                           array.word_size,
                           4))
      elif benchmark.name == "fft-transpose":
        if array.name != "work_x" and array.name != "work_y":
          config_file.write("partition,cyclic,%s,%d,%d,%d\n" %
                            (array.name,
                             array.size*array.word_size,
                             array.word_size,
                             4))
        else:
          config_file.write("cache,%s,%d,%d\n" %
                          (array.name,
                           array.size*array.word_size,
                           array.word_size))
      else:
        config_file.write("cache,%s,%d,%d\n" %
                        (array.name,
                         array.size*array.word_size,
                         array.word_size))
    else:
      print("Invalid memory type for array %s." % array.name)
      exit(1)

def write_benchmark_specific_configs(benchmark, config_file, params):
  """ Writes benchmark specific loop configurations.

  Returns True if configurations were affected, False otherwise.
  """
  # md-grid specific unrolling config
  if benchmark.name == "md-grid":
    config_file.write("flatten,md,loop_grid1_z\n")
    config_file.write("flatten,md,loop_p\n")
    config_file.write("flatten,md,loop_q\n")
    config_file.write("unrolling,md,loop_grid0_x,1\n")
    config_file.write("unrolling,md,loop_grid0_y,1\n")
    if params["unrolling"] <= 4:
      config_file.write("unrolling,md,loop_grid0_z,1\n")
      config_file.write("unrolling,md,loop_grid1_x,1\n")
      config_file.write("unrolling,md,loop_grid1_y,%d\n" % params["unrolling"])
    elif params["unrolling"] <= 16:
      config_file.write("unrolling,md,loop_grid0_z,1\n")
      config_file.write("unrolling,md,loop_grid1_x,%d\n" % params["unrolling"]/4)
      config_file.write("flatten,md,60\n")
    elif params["unrolling"] <= 32:
      config_file.write("unrolling,md,loop_grid0_z,%d\n" % params["unrolling"]/16)
      config_file.write("flatten,md,loop_grid1_x\n")
      config_file.write("flatten,md,loop_grid1_y\n")
    return True
  return False

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
  if "scratchpad_ports" in params:
    config_file.write("scratchpad_ports,%d\n" % params["scratchpad_ports"])
  if "ready_mode" in params:
    config_file.write("ready_mode,%d\n" % params["ready_mode"])
  if "pipelining" in params:
    config_file.write("pipelining,%d\n" % params["pipelining"])
  if "cycle_time" in params:
    config_file.write("cycle_time,%d\n" % params["cycle_time"])
  # TODO: Currently we're not separating arrays by kernel. This needs to
  # change.
  write_aladdin_array_configs(benchmark, config_file, params)
  specific_configs = write_benchmark_specific_configs(
      benchmark, config_file, params)

  if not specific_configs:
    for loop in loops:
      if loop.trip_count == UNROLL_FLATTEN:
        config_file.write("flatten,%s,%s\n" % (loop.name, loop.label))
      elif loop.trip_count == UNROLL_ONE:
        config_file.write("unrolling,%s,%s,%d\n" %
                          (loop.name, loop.label, loop.trip_count))
      elif (loop.trip_count == ALWAYS_UNROLL or
            params["unrolling"] < loop.trip_count):
        # We only unroll if it was specified to always unroll or if the loop's
        # trip count is greater than the current unrolling factor.
        config_file.write("unrolling,%s,%s,%d\n" %
                          (loop.name, loop.label, params["unrolling"]))
      elif params["unrolling"] >= loop.trip_count:
        config_file.write("flatten,%s,%s\n" % (loop.name, loop.label))
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
          "cycle time, area) 0:0:100:0:0\n"
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
      "-Add ECC - \"false\"\n"
      "-Print level (DETAILED, CONCISE) - \"DETAILED\"\n"
      "-Print input parameters - \"false\"\n"
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
    cache_assoc = L1CACHE_DEFAULTS["cache_assoc"]
  if "cache_line_sz" in params:
    cache_line_sz = params["cache_line_sz"]
  else:
    cache_line_sz = L1CACHE_DEFAULTS["cache_line_sz"]
  if "cache_size" in params:
    cache_size = params["cache_size"]
  else:
    cache_size = L1CACHE_DEFAULTS["cache_size"]
  if params["tlb_bandwidth"] == 0:
    rw_ports = 1
  else:
    rw_ports = params["tlb_bandwidth"]
  cache_params = {"cache_size": cache_size,
                  "cache_assoc": cache_assoc,
                  "rw_ports": rw_ports,
                  "exr_ports": 0, #params["load_bandwidth"],
                  "exw_ports": 0, #params["store_bandwidth"],
                  "line_size": cache_line_sz,
                  "banks" : 1, # One bank
                  "cache_type": "cache",
                  "search_ports": 0,
                  # Note for future reference - this may have to change per
                  # benchmark.
                  "io_bus_width" : 16 * 8}
  write_cacti_config(config_file, cache_params)
  config_file.close()

  config_file = open("%s_%s.cfg" % (kernel, CACTI_TLB_CFG), "wb")
  params["tlb_bandwidth"] = min(params["tlb_bandwidth"],
                                params["tlb_entries"])
  tlb_params = {"cache_size": params["tlb_entries"]*4,
                "cache_assoc": 0,  # fully associative
                "rw_ports": 0,
                "exw_ports":  1,  # One write port for miss returns.
                "exr_ports": rw_ports,
                "line_size": 4,  # 32b per TLB entry. in bytes
                "banks" : 1,
                "cache_type": "cache",
                "search_ports": rw_ports,
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
    l1cache_size = L1CACHE_DEFAULTS["cache_size"]/1024
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
      "%s_dynamic_trace.gz" % kernel if benchmark.separate_kernels else DYNAMIC_TRACE)

  for key in GEM5_DEFAULTS.iterkeys():
    if key in params:
      config.set(kernel, key, str(params[key]))

  # This is the only place where the sweep memory type needs to be
  # disambiguated like so.
  if params["memory_type"] & SPAD:
    if params["memory_type"] & CACHE:
      config.set(kernel, "memory_type", "hybrid")
    else:
      config.set(kernel, "memory_type", "spad")
  else:
    config.set(kernel, "memory_type", "cache")

  config.set(kernel, "input_dir", cur_config_file_dir)
  config.set(kernel, "bench_name",
             "%%(input_dir)s/outputs/%s" % kernel)
  config.set(kernel, "trace_file_name",
             "%s/inputs/%s" % (benchmark_top_dir, trace_file))
  config.set(kernel, "config_file_name",
             "%%(input_dir)s/%s.cfg" % kernel)
  if params["memory_type"] & CACHE:
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

    # Store queue is usually half of the size of load queue, but can't be zero!
    params["store_bandwidth"] = max(1, params["load_bandwidth"]/2)
    params["store_queue_size"] = max(1, params["load_queue_size"]/2)
    # Set max number of tlb outstanding walks the same as TLB sizes
    params["tlb_max_outstanding_walks"] = params["tlb_entries"]
    params["tlb_bandwidth"] = min(params["tlb_bandwidth"],
                                  params["tlb_entries"])

    for key, value in L1CACHE_DEFAULTS.iteritems():
      if not config.has_option(kernel, key):
        if key in params:
          config.set(kernel, key, str(params[key]))
        else:
          config.set(kernel, key, str(value))

  # Write the accelerator id and dependencies.
  kernel_id = benchmark.main_id  # By default.
  if benchmark.separate_kernels:
    kernel_id = benchmark.get_kernel_id(kernel)
    if kernel_id == -1:
      print("Something went wrong! "
            "%s was not found in the list of kernels." % kernel)
      exit(1)
  config.set(kernel, "accelerator_id", str(kernel_id))
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
    # step. If the parameter is linked to other, just use that other
    # parameter's value. If the parameter was set to NO_SWEEP, then we just use
    # the start value.
    if next_param.link_with:
      # We can only assign it the linked value if the linked parameter was
      # already assigned a value. If it was not, move this parameter to the end
      # of the list and keep going. If that parameter doesn't even exist, then
      # throw an error.
      linked_param = next_param.link_with
      if linked_param in set_params:
        value_range = [set_params[linked_param]]
      else:
        found_linked_param = False
        for p in local_sweep_params:
          if p.name == linked_param:
            local_sweep_params.insert(0, next_param)
            found_linked_param = True
            break
        if found_linked_param:
          generate_configs_recurse(benchmark, set_params, local_sweep_params,
                                   perfect_l1, enable_l2)
        else:
          raise KeyError("The linked parameter %s on %s does not exist." %
                         (linked_param, next_param.name))
    elif next_param.step_type == NO_SWEEP:
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
      if next_param.linked_to in set_params:
        # This parameter is linked to another parameter. When we change this
        # one, we have to change the other one too.
        set_params[next_param.linked_to] = value
      generate_configs_recurse(benchmark, set_params, local_sweep_params,
                               perfect_l1, enable_l2)
  else:
    # All parameters have been populated with values. We can write the
    # configuration now.

    # The config will use load_bandwidth as "bandwidth" in the naming since it's
    # probable that even memory-bound accelerators are read-bound and not
    # write-bound.
    if set_params["memory_type"] & SPAD and not set_params["memory_type"] & CACHE:
      config_name = generate_smart_config_name(set_params)
    elif set_params["memory_type"] & CACHE:
      #if perfect_l1:
        #CONFIG_NAME_FORMAT = "pipe%d_unr_%d_tlb_%d_ldbw_%d_ldq_%d"
        #config_name = CONFIG_NAME_FORMAT % (set_params["pipelining"],
                                            #set_params["unrolling"],
                                            #set_params["tlb_entries"],
                                            #set_params["load_bandwidth"],
                                            #set_params["load_queue_size"])
      #else:
      config_name = generate_smart_config_name(set_params)
    else:
      raise ValueError("Unrecognized memory_type.")
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
      generate_gem5_config(
          benchmark, kernel, set_params, write_new=new_gem5_config)
      if set_params["memory_type"] & CACHE:
        generate_all_cacti_configs(benchmark.name, kernel, set_params)
      new_gem5_config = False
    if enable_l2:
      l2cache_params = dict(set_params)
      l2cache_params["cache_size"] = set_params["l2cache_size"]
      write_cacti_config(benchmark.name, l2cache_params)
    os.chdir("..")

def generate_all_configs(
    benchmark, memory_type, experiment_name, perfect_l1, enable_l2):
  """ Generates all the possible configurations for the design sweep. """
  # Start out with these parameters.
  all_sweep_params = [cycle_time,
                      pipelining,
                      unrolling]
  if memory_type & SPAD:
    all_sweep_params.extend([partition, scratchpad_ports])
  if memory_type & DMA:
    all_sweep_params.extend([ready_mode,
                             dma_multi_channel,
                             issue_dma_ops_asap,
                             ignore_cache_flush])
  if memory_type & CACHE:
    if perfect_l1:
      all_sweep_params.extend([tlb_bandwidth,
                               tlb_entries,
                               load_bandwidth,
                               load_queue_size])
    else:
      all_sweep_params.extend([tlb_entries,
                               tlb_bandwidth,
                               load_bandwidth,
                               load_queue_size,
                               cache_hit_latency,
                               cache_assoc,
                               cache_line_sz,
                               cache_size])
  # This dict stores a single configuration.
  params = {"memory_type": memory_type, "experiment_name": experiment_name}
  # Recursively generate all possible configurations.
  print benchmark.kernels
  # Create bidirectional mapping for all linked parameters.
  for curr_param in all_sweep_params:
    if curr_param.link_with:
      # Find the linked parameter.
      for other_param in all_sweep_params:
        if curr_param.link_with == other_param.name:
          other_param.linked_to = curr_param.name

  generate_configs_recurse(benchmark, params, all_sweep_params,
                           perfect_l1, enable_l2)

def write_config_files(benchmark, output_dir, memory_type,
                       experiment_name, perfect_l1, enable_l2):
  """ Create the directory structure and config files for a benchmark. """
  # This assumes we're already in output_dir.
  if not os.path.exists(benchmark.name):
    os.makedirs(benchmark.name)
  print "Generating configurations for %s" % benchmark.name
  os.chdir(benchmark.name)
  generate_all_configs(
      benchmark, memory_type, experiment_name, perfect_l1, enable_l2)
  os.chdir("..")

def run_sweeps(workload, simulator, output_dir, source_dir, dry_run, enable_l2,
               perfect_l1, perfect_bus, experiment_name):
  """ Run the design sweep on the given workloads.

  This function will also write a convenience Bash script to the configuration
  directory so a user can manually run a single simulation directly.  During a
  dry run, these scripts will still be written, but the simulations themselves
  will not be executed.

  Args:
    workload: List of benchmark description objects.
    simulator: Specifies the simulator to use, gem5 or aladdin.
    output_dir: Top-level directory of the configuration files.
    source_dir: Source code directory.
    dry_run: True for a dry run.
    enable_l2: Use a shared L2 last level cache.
    perfect_l1: Use a perfect L1 cache.
    experiment_name: Label the experiment in the database if using the DB.
  """
  cwd = os.getcwd()
  gem5_home = cwd.split("sweeps")[0]
  print gem5_home
  # Turning on debug outputs for CacheDatapath can incur a huge amount of disk
  # space, most of which is redundant, so we leave that out of the command here.
  run_cmd = ""
  num_cpus = 0
  if "cpu" in simulator:
    num_cpus = 1
  if simulator.startswith("gem5"):
    run_cmd = ("%(gem5_home)s/build/X86/gem5.opt "
               #"--stats-db-file=stats.db "
               "--outdir=%(output_path)s "
               "%(gem5_home)s/configs/aladdin/aladdin_se.py "
               "--num-cpus=%(num_cpus)s "
               "--mem-size=4GB "
               "--enable-stats-dump "
               "--enable_prefetchers --prefetcher-type=stride "
               "%(mem_flag)s "
               "--sys-clock=%(sys_clock)s "
               #"--l1d_size=4MB --l1d_assoc=16 "
               "--cpu-type=timing --caches %(l2cache_flag)s "
               "--cacheline_size=32 "
               "%(perfect_l1_flag)s "
               "%(perfect_bus_flag)s "
               "--aladdin_cfg_file=%(aladdin_cfg_path)s "
               "%(executable)s %(run_args)s "
               "> %(output_path)s/stdout 2> %(output_path)s/stderr")
  else:
    run_cmd = ("%(aladdin_home)s/common/aladdin "
               "%(output_path)s/%(benchmark_name)s "
               "%(bmk_dir)s/inputs/%(trace_name)s_trace.gz "
               "%(config_path)s/%(benchmark_name)s.cfg "
               "%(experiment_name)s "
               "> %(output_path)s/%(benchmark_name)s_stdout "
               "2> %(output_path)s/%(benchmark_name)s_stderr")
  os.chdir("..")
  # TODO: Since the L2 cache is such an important flag, I'm hardcoding in a few
  # parameters specific to it so we can quickly run experiments with and without
  # it. We should reimplement this later so it's more general.
  l2cache_flag = "--l2cache" if enable_l2 else ""
  mem_flag = "--mem-latency=0ns --mem-type=simple_mem " if perfect_l1 else "--mem-type=DDR3_1600_x64 "
  perfect_l1_flag = "--is_perfect_cache=1 --is_perfect_bus=1 " if perfect_l1 else ""
  perfect_bus_flag = "--is_perfect_bus=1 " if perfect_bus  else ""
  if enable_l2:
    file_name = "run_L2.sh"
  elif perfect_l1:
    file_name = "run_perfect_l1.sh"
  else:
    file_name = "run.sh"
  for benchmark in workload:
    print "------------------------------------"
    print "Executing benchmark %s" % benchmark.name
    bmk_dir = "%s/%s/%s" % (cwd, output_dir, benchmark.name)
    configs = [file for file in os.listdir(bmk_dir)
               if os.path.isdir("%s/%s" % (bmk_dir, file))]
    executable = ""
    run_args = ""
    expansion_args = {"source_dir": source_dir}
    if simulator == "gem5-cpu":
      executable = "-c %s" % (benchmark.expand_exec_cmd(expansion_args))
      run_args = "-o \"%s\"" % (benchmark.expand_run_args(expansion_args))
    for config in configs:
      config_path = "%s/%s" % (bmk_dir, config)
      abs_cfg_path = "%s/%s/%s" % (bmk_dir, config, GEM5_CFG)
      if not os.path.exists(abs_cfg_path):
        continue
      abs_output_path = "%s/%s/outputs" % (bmk_dir, config)
      cmd = ""
      if simulator.startswith("gem5"):
        sys_clock = "100MHz"
        cmd = run_cmd % {"gem5_home": gem5_home,
                         "output_path": abs_output_path,
                         "aladdin_cfg_path": abs_cfg_path,
                         "num_cpus": num_cpus,
                         "l2cache_flag": l2cache_flag,
                         "mem_flag": mem_flag,
                         "perfect_l1_flag" : perfect_l1_flag,
                         "perfect_bus_flag" : perfect_bus_flag,
                         "executable": executable,
                         "run_args": run_args,
                         "sys_clock": sys_clock}
      else:
        if not experiment_name:
          experiment_name = ""
        if benchmark.separate_kernels:
          # If the workload has separated kernels, we need to run Aladdin on
          # each of the kernels.
          for kernel in benchmark.kernels:
              cmd = cmd + "\n" + run_cmd % {
                "aladdin_home": os.environ["ALADDIN_HOME"],
                "benchmark_name": kernel,
                "trace_name": kernel,
                "output_path": abs_output_path,
                "bmk_dir": bmk_dir,
                "config_path": config_path,
                "experiment_name": experiment_name}
        else:
          cmd = run_cmd % {"aladdin_home": os.environ["ALADDIN_HOME"],
                           "benchmark_name": benchmark.name,
                           "trace_name": "dynamic",
                           "output_path": abs_output_path,
                           "bmk_dir": bmk_dir,
                           "config_path": config_path,
                           "experiment_name": experiment_name}

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

def generate_traces(workload, output_dir, source_dir, memory_type, simulator):
  """ Generates dynamic traces for each workload.

  This is accomplished by invoking the benchmark suite's own Makefiles.  The
  Makefiles must implement the following build targets:

    1. clean-trace: Deletes any existing traces, object files, LLVM IR, and any
       other intermediate files associated with building the instrumented binary.
    2. trace-binary: Build the instrumented binary.
    3. dma-trace-binary: Same as above, but with -DDMA_MODE in CFLAGS.
    4. run-trace: Execute the instrumented binary to produce the trace
       (dynamic_trace.gz).
  """
  if not "TRACER_HOME" in os.environ:
    raise Exception("Set TRACER_HOME directory as an environment variable")
  fdevnull = open(os.devnull, "w")
  cwd = os.getcwd()
  for benchmark in workload:
    print "Building traces for", benchmark.name
    bmk_source_dir = os.path.join(source_dir, benchmark.source_file);
    trace_orig_path = os.path.join(bmk_source_dir, DYNAMIC_TRACE)
    os.chdir(bmk_source_dir)
    trace_target = "trace-binary" if not memory_type & DMA else "dma-trace-binary"

    # Clean, rebuild the instrumented binary, and regenerate the trace.
    ret = subprocess.call("make clean-trace",
                          stdout=fdevnull, stderr=subprocess.STDOUT, shell=True)
    if ret:
      print "[ERROR] Failed to clean the existing trace."
      continue
    ret = subprocess.call("make %s" % trace_target,
                          stdout=fdevnull, stderr=subprocess.STDOUT, shell=True)
    if ret:
      print "[ERROR] Failed to build the instrumented binary. Skipping trace generation."
      continue
    ret = subprocess.call("make run-trace",
                          stdout=fdevnull, stderr=subprocess.STDOUT, shell=True)
    if ret:
      print "[ERROR] Failed to execute the instrumented binary and generate the trace."
      continue

    # Move them to the right place.
    trace_abs_dir = os.path.abspath(os.path.join(cwd, output_dir, benchmark.name, "inputs"))
    if not os.path.isdir(trace_abs_dir):
      os.makedirs(trace_abs_dir)
    # Moves and overwrites a trace at the destination if it already exists.
    os.rename(trace_orig_path, os.path.join(trace_abs_dir, DYNAMIC_TRACE))

    # Cleanup.
    ret = subprocess.call("make clean-trace",
                          stdout=fdevnull, stderr=subprocess.STDOUT, shell=True)
    if ret:
      print "[ERROR] Failed to clean up after building and generating traces."

  fdevnull.close()
  os.chdir(cwd)
  return

def generate_condor_scripts(workload, simulator, output_dir, source_dir,
                            username, enable_l2, perfect_l1, perfect_bus, experiment_name):
  """ Generate a single Condor script for the complete design sweep. """
  # First, generate all the runscripts.
  run_sweeps(workload, simulator, output_dir, source_dir, True, enable_l2,
             perfect_l1, perfect_bus, experiment_name)
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
  f = open("%s/%s/%s" % (cwd, output_dir, condor_file_name), "w")
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
      f.write("Log = %s/%s/log\n" % (bmk_dir, config))
      f.write("Queue\n\n")
  f.close()
  os.chdir(cwd)

def check_arguments(args):
  """ Preliminary checking on command-line arguments. """
  # Required arguments for all modes.
  if not args.output_dir:
    sys.exit("You must specify an output directory to contain the sweep!")
  if not args.benchmark_suite:
    sys.exit("You must specify a benchmark suite to use for the sweep!")
  if not args.benchmark_suite.upper() in benchmarks:
    print ("Unrecognized benchmark suite %s. Available benchmark suites are: "
           % args.benchmark_suite)
    for benchmark in benchmarks.iterkeys():
      print "  ", benchmark
    sys.exit(1)

  # Convert memory type to integer flag.
  if args.memory_type == "spad":
    args.memory_type = SPAD
  elif args.memory_type == "dma":
    args.memory_type = DMA | SPAD
  elif args.memory_type == "cache":
    args.memory_type = CACHE
  elif args.memory_type == "hybrid":
    args.memory_type = SPAD | CACHE
  # Per mode requirements
  if args.mode == "all":
    args.dry = True

  if args.mode == "configs" or args.mode == "all":
    if not args.memory_type:
      sys.exit("Missing memory_type argument. See help documentation (-h).")

  if args.mode == "trace" or args.mode == "all":
    if not args.source_dir:
      sys.exit("Need to specify the benchmark suite source directory!")
    if not args.memory_type:
      sys.exit("Missing memory_type argument. See help documentation (-h).")

  if args.mode == "condor" or args.mode == "all":
    if not args.username:
      args.username = getpass.getuser()
      print "Username was not specified for Condor. Using %s." % args.username

def main():
  parser = argparse.ArgumentParser(
      description="Generate and run Aladdin design sweeps.",
      formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument(
      "mode", choices=["configs", "trace", "run", "all", "condor"], help=
      "Run mode. \"configs\" will generate all possible configurations for the "
      "desired sweep. \"trace\" will build dynamic traces for all benchmarks. "
      "\"run\" will run the generated design sweep for a benchmark suite. "
      "\"condor\" will write a condor job script for the complete design sweep."
      " \"all\" will do all of the above EXCEPT for actually running the "
      "experiment. ")
  parser.add_argument("--output_dir", required=True, help="Config output "
                      "directory. Required for all modes.")
  parser.add_argument("--memory_type", help="\"cache\",\"spad\" or \"dma\", or "
      "\"hybrid\" (which combines cache and spad).")
  parser.add_argument("--benchmark_suite", required=True, help="The name of "
      "the benchmark suite which for to a run sweep. The benchmark must have a "
      "config script under the benchmark_configs/ directory. Required for all "
      "modes.")
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
      "cache during simulations if applicable.")
  parser.add_argument("--perfect_l1", action="store_true", help="Enable the "
      "perfect l1 cache during simulations if applicable. This will cause the "
      "simulation output to be dumped to a directory called outputs/perfect_l1."
      "Without this flag, GEM5 output is stored to outputs/no_L2. If "
      "generating Condor scripts, this flag will run perfect l1 simulations.")
  parser.add_argument("--perfect_bus", action="store_true", help="Enable the "
      "perfect memory bus during simulations if applicable. ")
  parser.add_argument("--simulator", choices=["aladdin", "gem5-cache", "gem5-cpu"],
      default="aladdin", help="Select the simulator to use, gem5 or aladdin. "
      "If selecting gem5, decide whether to include a CPU or just use the memory "
      "hierarchy.")
  args = parser.parse_args()
  check_arguments(args)

  workload = benchmarks[args.benchmark_suite.upper()]
  if args.mode == "configs" or args.mode == "all":
    current_dir = os.getcwd()
    if not os.path.exists(args.output_dir):
      os.makedirs(args.output_dir)
    os.chdir(args.output_dir)

    for benchmark in workload:
      write_config_files(benchmark, args.output_dir, args.memory_type,
                         args.experiment_name, args.perfect_l1, args.enable_l2)
    os.chdir(current_dir)

  if args.mode == "trace" or args.mode == "all":
    generate_traces(workload, args.output_dir, args.source_dir,
                    args.memory_type, args.simulator)

  if args.mode == "run":
    run_sweeps(workload, args.simulator, args.output_dir, args.source_dir,
               args.dry, args.enable_l2, args.perfect_l1, args.perfect_bus, args.experiment_name)

  if args.mode == "condor" or args.mode == "all":
    generate_condor_scripts(
        workload, args.simulator, args.output_dir, args.source_dir, args.username,
        args.enable_l2, args.perfect_l1, args.perfect_bus, args.experiment_name)

if __name__ == "__main__":
  main()
