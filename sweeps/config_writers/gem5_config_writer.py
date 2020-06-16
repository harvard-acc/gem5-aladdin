# Writes JSON design sweep dumps as gem5 configuration files.

import configparser
import os

from benchmarks import datatypes
from benchmarks import params
from config_writers import config_writer
from xenon.base.datatypes import Param

GEM5_HOME = os.path.abspath(os.path.join(os.environ["ALADDIN_HOME"], "..", ".."))
GEM5_PATH = os.path.join(GEM5_HOME, "build", "X86", "gem5.opt")
DEFAULTS = {}

def buildParamDefaults():
  defaults = {}
  for attr_name, attr_value in params.__dict__.items():
    if isinstance(attr_value, Param):
      defaults[attr_name] = attr_value.format(attr_value.default)
  return defaults

class Gem5ConfigWriter(config_writer.JsonConfigWriter):
  """ Writes ini-file for gem5 plus CACTI cfgs. """
  def __init__(self):
    super(Gem5ConfigWriter, self).__init__()
    self.topLevelType = "Benchmark"
    global DEFAULTS
    DEFAULTS = buildParamDefaults()

  def is_applicable(self, sweep):
    if not "simulator" in sweep:
      return False
    simulator = sweep["simulator"]
    is_applicable = (simulator == "gem5-cache" or simulator == "gem5-cpu")
    self.generate_runscripts = is_applicable
    return is_applicable

  def writeSweep(self, sweep):
    """ Write all configuration files. """
    cwd = os.getcwd()
    base_output_dir = os.path.join(cwd, sweep["output_dir"])
    for name, child_obj in self.iterdicttypes(sweep):
      identifier = self.get_identifier(name)
      assert(identifier)

      if identifier == self.topLevelType:
        # The top level object has to prepare the output directories
        # and actually dump the output.
        benchmark = child_obj["name"]
        sweep_dir = self.getOutputSweepDirectory(sweep, benchmark)
        output_dir = os.path.join(sweep_dir, "outputs")
        if not os.path.exists(output_dir):
          os.makedirs(output_dir)

        self.writeGem5Config(sweep, child_obj, sweep_dir)
        self.writeAllCactiConfigs(sweep, child_obj, sweep_dir)
        self.setupRequiredFiles(child_obj, sweep_dir, sweep)
        self.writeRunscript(sweep, child_obj, sweep_dir)

  def writeLast(self, all_sweeps):
    pass

  def addGlobalSweepParams(self, sweep, section, config_writer):
    global_params = ["memory_type"]
    for param in global_params:
      config_writer.set(section, param, sweep[param])

  def getCactiConfigPaths(self, benchmark_name, sweep_dir):
    return {
        "cache": os.path.join(sweep_dir, "%s-cache.cfg" % benchmark_name),
        "tlb": os.path.join(sweep_dir, "%s-tlb.cfg" % benchmark_name),
        "queue": os.path.join(sweep_dir, "%s-queue.cfg" % benchmark_name),
    }

  def setupRequiredFiles(self, this_config, sweep_dir, parent_sweep):
    """ Symlink all required files into the sweep directory.

    Args:
      this_config: The JSON object for this sweep configuration.
      sweep_dir: The directory this configuration is going into.
      parent_sweep: The parent of this JSON object (a BaseDesignSweep).
    """
    cwd = os.getcwd()
    bmk_name = this_config["name"]
    bmk_src_dir = os.path.join(parent_sweep["source_dir"], this_config["sub_dir"])
    if not os.path.isabs(bmk_src_dir):
      bmk_src_dir = os.path.realpath(bmk_src_dir)
    for req_file in this_config["required_files"]:
      source = os.path.join(bmk_src_dir, req_file)
      link = os.path.join(sweep_dir, os.path.basename(req_file))
      if os.path.lexists(link):
        os.remove(link)
      os.symlink(source, link)

    # If we are simulating with the CPU model, symlink the CPU binaries into
    # the config directory and fail should they not exist.
    if parent_sweep["simulator"] == "gem5-cpu":
      nonaccel_bin = os.path.join(bmk_src_dir, "%s-gem5" % bmk_name)
      accel_bin = os.path.join(bmk_src_dir, "%s-gem5-accel" % bmk_name)
      if not (os.path.exists(nonaccel_bin) and os.path.exists(accel_bin)):
        raise IOError("Benchmarks have not been completely built. Did you add "
                      "\"generate \"gem5_binary\" to the sweep script?")
      link = os.path.join(sweep_dir, "%s-gem5" % bmk_name)
      if os.path.lexists(link):
        os.remove(link)
      os.symlink(nonaccel_bin, link)
      link = os.path.join(sweep_dir, "%s-gem5-accel" % bmk_name)
      if os.path.lexists(link):
        os.remove(link)
      os.symlink(accel_bin, link)

  def writeGem5Config(self, sweep, benchmark, sweep_dir):
    """ Write the gem5.cfg file. """
    output_dir = os.path.join(sweep_dir, "outputs")
    benchmark_name = benchmark["name"]
    benchmark_name_section = benchmark["name"].replace("-", "")
    output_prefix = os.path.join(output_dir, benchmark_name)

    config_writer = configparser.ConfigParser(DEFAULTS)
    config_writer.add_section(benchmark_name_section)
    # Add the non sweepable parameters.
    self.addGlobalSweepParams(sweep, benchmark_name_section, config_writer)
    config_writer.set(benchmark_name_section, "accelerator_id", str(benchmark["main_id"]))
    config_writer.set(benchmark_name_section, "bench_name", output_prefix)
    config_writer.set(benchmark_name_section, "trace_file_name",
                      os.path.join(sweep_dir, "..", "inputs", "dynamic_trace.gz"))
    config_writer.set(benchmark_name_section, "config_file_name",
                      os.path.join(sweep_dir, "%s.cfg" % benchmark_name))
    cacti_configs = self.getCactiConfigPaths(benchmark_name, sweep_dir)
    config_writer.set(benchmark_name_section, "cacti_cache_config", cacti_configs["cache"])
    config_writer.set(benchmark_name_section, "cacti_tlb_config", cacti_configs["tlb"])

    # These DB options shouldn't ever be used unless the user has set up a SQL
    # server, in which case they should set these themselves.
    config_writer.set(benchmark_name_section, "use_db", "False")
    config_writer.set(benchmark_name_section, "experiment_name", "")

    # Add all sweepable parameters.
    output_params = [param for param in datatypes.Benchmark.sweepable_params]
    for param in output_params:
      value = benchmark[param.name]
      config_writer.set(benchmark_name_section, param.name, param.format(value))

    output_file = os.path.join(sweep_dir, "gem5.cfg")
    with open(output_file, "w") as f:
      config_writer.write(f)

  def writeAllCactiConfigs(self, sweep, benchmark, sweep_dir):
    cacti_config_files = self.getCactiConfigPaths(benchmark["name"], sweep_dir)
    rw_ports = max(1, benchmark["tlb_bandwidth"])
    cache_params = {"cache_size": benchmark["cache_size"],
                    "cache_assoc": benchmark["cache_assoc"],
                    "rw_ports": rw_ports,
                    "exr_ports": 0, #params["load_bandwidth"],
                    "exw_ports": 0, #params["store_bandwidth"],
                    "line_size": benchmark["cache_line_sz"],
                    "banks" : 1, # One bank
                    "cache_type": "cache",
                    "search_ports": 0,
                    # Note for future reference - this may have to change per
                    # benchmark.
                    "io_bus_width" : 16 * 8}
    with open(cacti_config_files["cache"], "w") as f:
      self.writeCactiConfigFile_(f, cache_params)

    tlb_params = {"cache_size": benchmark["tlb_entries"]*8,
                  "cache_assoc": 0,  # fully associative
                  "rw_ports": 0,
                  "exw_ports":  1,  # One write port for miss returns.
                  "exr_ports": rw_ports,
                  "line_size": 8,  # 64b per TLB entry. in bytes
                  "banks" : 1,
                  "cache_type": "cache",
                  "search_ports": rw_ports,
                  "io_bus_width" : 64}
    with open(cacti_config_files["tlb"], "w") as f:
      self.writeCactiConfigFile_(f, tlb_params)

    queue_params = {"cache_size": benchmark["cache_queue_size"]*8,
                    "cache_assoc": 0,  # fully associative
                    "rw_ports": rw_ports,
                    "exw_ports": 0,
                    "exr_ports": 0,
                    "line_size": 8,  # 64b (ignoring a few extra for status).
                    "banks" : 1,
                    "cache_type": "cache",
                    "search_ports": rw_ports,
                    "io_bus_width": 64}
    with open(cacti_config_files["queue"], "w") as f:
      self.writeCactiConfigFile_(f, queue_params)

  def writeCactiConfigFile_(self, config_file, params):
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

  def writeRunscript(self, sweep, benchmark, sweep_dir):
    output_dir = os.path.join(sweep_dir, "outputs")
    runscript_path = os.path.join(sweep_dir, "run.sh")
    gem5_cfg_path = os.path.join(sweep_dir, "gem5.cfg")
    aladdin_script = os.path.join(GEM5_HOME, "configs", "aladdin", "aladdin_se.py")
    num_cpus = 1 if sweep["simulator"] == "gem5-cpu" else 0
    sys_clock = self.getSysClock(benchmark)
    stdout_path = os.path.join(output_dir, "stdout")
    stderr_path = os.path.join(output_dir, "stderr")
    l2cache_flag = "--l2cache" if benchmark["enable_l2"] else ""
    perfect_bus_flag = "--is_perfect_bus=1 " if benchmark["perfect_bus"] else ""
    if benchmark["perfect_l1"]:
      mem_flag = "--mem-latency=0ns --mem-type=simple_mem "
      perfect_l1_flag = "--is_perfect_cache=1 --is_perfect_bus=1"
    else:
      mem_flag = "--mem-type=DDR3_1600_8x8 "
      perfect_l1_flag = ""

    if benchmark["exec_cmd"]:
      exec_cmd = "-c {0} -o \"{1}\"".format(
          benchmark["exec_cmd"], benchmark["run_args"])
    else:
      exec_cmd = ""

    with open(runscript_path, "w") as f:
      lines = [
          "#!/bin/sh",
          GEM5_PATH,
          "--stats-db-file=stats.db",
          "--outdir=" + output_dir,
          aladdin_script,
          "--num-cpus=" + str(num_cpus),
          "--mem-size=4GB",
          "--enable-stats-dump",
          "--enable_prefetchers",
          "--prefetcher-type=stride",
          mem_flag,
          "--sys-clock=" + sys_clock,
          "--cpu-type=DerivO3CPU ",
          "--caches",
          l2cache_flag,
          "--cacheline_size=%d " % benchmark["cache_line_sz"],
          perfect_l1_flag,
          perfect_bus_flag,
          "--accel_cfg_file=" + gem5_cfg_path,
          exec_cmd,
          "> " + stdout_path,
          "2> " + stderr_path,
      ]
      f.write(" \\\n".join(lines))

  def getSysClock(self, benchmark):
    """ Convert cycle time into MHz. """
    clock_period = float(benchmark["cycle_time"]) * 1e-9
    freq = 1.0/clock_period
    mhz = freq / 1e6
    return "%dMHz" % mhz

  def shortToLongSize(self, size_str):
    """ Go from 16kB -> 16384. """
    if size_str.endswith("kB"):
      return int(size_str[:-2])*1024
    elif size_str.endswith("MB"):
      return int(size_str[:-2])*1024*1024
    elif size_str.endswith("GB"):
      return int(size_str[:-2])*1024*1024*1024
    elif size_str.endswith("B"):
      # If we're dealing with anything larger than what can be expressed in GB,
      # there's a problem.
      return int(size_str[:-2])
    else:
      raise ValueError("Size \"%s\" cannot be converted into bytes." % size_str)
