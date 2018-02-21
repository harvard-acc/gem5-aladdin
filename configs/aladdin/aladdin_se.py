# Copyright (c) 2012-2013 ARM Limited
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2006-2008 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Steve Reinhardt

# This is a system emulation script with Aladdin accelerators.
#
# "m5 test.py"

import ConfigParser
import optparse
import sys
import os

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import addToPath, fatal

addToPath('../')

from ruby import Ruby
from common import Options
from common import Simulation
from common import CacheConfig
from common import MemConfig

from common.Caches import *
from common.cpu2000 import *

def addAladdinOptions(parser):
    parser.add_option("--accel_cfg_file", default=None,
        help="Aladdin accelerator configuration file.")
    parser.add_option("--aladdin-debugger", action="store_true",
        help="Run the Aladdin debugger on accelerator initialization.")

def get_processes(options):
    """Interprets provided options and returns a list of processes"""

    multiprocesses = []
    inputs = []
    outputs = []
    errouts = []
    pargs = []

    workloads = options.cmd.split(';')
    if options.input != "":
        inputs = options.input.split(';')
    if options.output != "":
        outputs = options.output.split(';')
    if options.errout != "":
        errouts = options.errout.split(';')
    if options.options != "":
        pargs = options.options.split(';')

    idx = 0
    for wrkld in workloads:
        process = Process()
        process.executable = wrkld
        process.cwd = os.getcwd()

        if options.env:
            with open(options.env, 'r') as f:
                process.env = [line.rstrip() for line in f]

        if len(pargs) > idx:
            process.cmd = [wrkld] + pargs[idx].split()
        else:
            process.cmd = [wrkld]

        if len(inputs) > idx:
            process.input = inputs[idx]
        if len(outputs) > idx:
            process.output = outputs[idx]
        if len(errouts) > idx:
            process.errout = errouts[idx]

        multiprocesses.append(process)
        idx += 1

    if options.smt:
        assert(options.cpu_type == "detailed" or options.cpu_type == "inorder")
        return multiprocesses, idx
    else:
        return multiprocesses, 1


parser = optparse.OptionParser()
Options.addCommonOptions(parser)
Options.addSEOptions(parser)
addAladdinOptions(parser)

if '--ruby' in sys.argv:
    Ruby.define_options(parser)

(options, args) = parser.parse_args()

if args:
    print "Error: script doesn't take any positional arguments"
    sys.exit(1)

multiprocesses = []
numThreads = 1

np = options.num_cpus
if np > 0:
  if options.bench:
      apps = options.bench.split("-")
      if len(apps) != options.num_cpus:
          print "number of benchmarks not equal to set num_cpus!"
          sys.exit(1)

      for app in apps:
          try:
              if buildEnv['TARGET_ISA'] == 'alpha':
                  exec("workload = %s('alpha', 'tru64', 'ref')" % app)
              else:
                  exec("workload = %s(buildEnv['TARGET_ISA'], 'linux', 'ref')" % app)
              multiprocesses.append(workload.makeProcess())
          except:
              print >>sys.stderr, "Unable to find workload for %s: %s" % (buildEnv['TARGET_ISA'], app)
              sys.exit(1)
  elif options.cmd:
      multiprocesses, numThreads = get_processes(options)
  else:
      print >> sys.stderr, "No workload specified. Exiting!\n"
      sys.exit(1)


(CPUClass, test_mem_mode, FutureClass) = Simulation.setCPUClass(options)
#print "CPUClass:%s, test_mem_mode:%s, FutureClass:%s" % (CPUClass, test_mem_mode, FutureClass)
CPUClass.numThreads = numThreads

MemClass = Simulation.setMemClass(options)

# Check -- do not allow SMT with multiple CPUs
if options.smt and options.num_cpus > 1:
    fatal("You cannot use SMT with multiple CPUs!")

system = System(mem_mode = test_mem_mode,
                mem_ranges = [AddrRange(options.mem_size)],
                cache_line_size = options.cacheline_size)

# The O3 model requires fetch buffer size at most the cache line size.
if CPUClass.type == 'DerivO3CPU':
  CPUClass.fetchBufferSize = min(CPUClass.fetchBufferSize, system.cache_line_size)

# Create a top-level voltage domain
system.voltage_domain = VoltageDomain(voltage = options.sys_voltage)

# Create a source clock for the system and set the clock period
system.clk_domain = SrcClockDomain(clock = options.sys_clock,
                                   voltage_domain = system.voltage_domain)

# Create a CPU voltage domain
system.cpu_voltage_domain = VoltageDomain()

# Create a separate clock domain for the CPUs
system.cpu_clk_domain = SrcClockDomain(clock = options.cpu_clock,
                                       voltage_domain =
                                       system.cpu_voltage_domain)

if np > 0:
  system.cpu = [CPUClass(cpu_id=i) for i in xrange(np)]
  # All cpus belong to a common cpu_clk_domain, therefore running at a common
  # frequency.
  for cpu in system.cpu:
      cpu.clk_domain = system.cpu_clk_domain

if options.accel_cfg_file:
  # First read all default values.
  default_cfg = ConfigParser.SafeConfigParser()
  default_cfg_file = os.path.join(
      os.path.dirname(os.path.realpath(__file__)), "aladdin_template.cfg")
  default_cfg.read(default_cfg_file)
  defaults = dict(i for i in default_cfg.items("DEFAULT"))

  # Now read the actual supplied config file using the defaults.
  config = ConfigParser.SafeConfigParser(defaults)
  config.read(options.accel_cfg_file)
  accels = config.sections()
  if not accels:
    fatal("No accelerators were specified!")
  datapaths = []
  for accel in accels:
    memory_type = config.get(accel, 'memory_type').lower()
    # Accelerators need their own clock domain!
    cycleTime = config.getint(accel, "cycle_time")
    clock = "%1.3fGHz" % (1/cycleTime)
    clk_domain = SrcClockDomain(
        clock = clock, voltage_domain = system.cpu_voltage_domain)
    # Set the globally required parameters.
    datapath = HybridDatapath(
        clk_domain = clk_domain,
        benchName = accel,
        # TODO: Ideally bench_name would change to output_prefix but that's a
        # pretty big breaking change.
        outputPrefix = config.get(accel, "bench_name"),
        traceFileName = config.get(accel, "trace_file_name"),
        configFileName = config.get(accel, "config_file_name"),
        acceleratorName = "%s_datapath" % accel,
        acceleratorId = config.getint(accel, "accelerator_id"),
        cycleTime = cycleTime,
        useDb = config.getboolean(accel, "use_db"),
        experimentName = config.get(accel, "experiment_name"),
        enableStatsDump = options.enable_stats_dump_and_resume,
        executeStandalone = (np == 0))
    datapath.cacheLineFlushLatency = config.getint(accel, "cacheline_flush_latency")
    datapath.cacheLineInvalidateLatency = config.getint(accel, "cacheline_invalidate_latency")
    datapath.dmaSetupOverhead = config.getint(accel, "dma_setup_overhead")
    datapath.maxDmaRequests = config.getint(accel, "max_dma_requests")
    datapath.numDmaChannels = config.getint(accel, "num_dma_channels")
    datapath.dmaChunkSize = config.getint(accel, "dma_chunk_size")
    datapath.pipelinedDma = config.getboolean(accel, "pipelined_dma")
    datapath.ignoreCacheFlush = config.getboolean(accel, "ignore_cache_flush")
    datapath.invalidateOnDmaStore = config.getboolean(accel, "invalidate_on_dma_store")
    datapath.recordMemoryTrace = config.getboolean(accel, "record_memory_trace")
    datapath.enableAcp = config.getboolean(accel, "enable_acp")
    datapath.useAcpCache = True
    datapath.acpCacheLatency = config.getint(accel, "acp_cache_latency")
    datapath.acpCacheMSHRs = config.getint(accel, "acp_cache_mshrs")
    datapath.useAladdinDebugger = options.aladdin_debugger
    if memory_type == "cache":
      datapath.cacheSize = config.get(accel, "cache_size")
      datapath.cacheBandwidth = config.get(accel, "cache_bandwidth")
      datapath.cacheQueueSize = config.get(accel, "cache_queue_size")
      datapath.cacheAssoc = config.getint(accel, "cache_assoc")
      datapath.cacheHitLatency = config.getint(accel, "cache_hit_latency")
      datapath.cacheLineSize = options.cacheline_size
      datapath.cactiCacheConfig = config.get(accel, "cacti_cache_config")
      datapath.tlbEntries = config.getint(accel, "tlb_entries")
      datapath.tlbAssoc = config.getint(accel, "tlb_assoc")
      datapath.tlbHitLatency = config.getint(accel, "tlb_hit_latency")
      datapath.tlbMissLatency = config.getint(accel, "tlb_miss_latency")
      datapath.tlbCactiConfig = config.get(accel, "cacti_tlb_config")
      datapath.tlbPageBytes = config.getint(accel, "tlb_page_size")
      datapath.numOutStandingWalks = config.getint(
          accel, "tlb_max_outstanding_walks")
      datapath.tlbBandwidth = config.getint(accel, "tlb_bandwidth")
    elif memory_type == "spad" and options.ruby:
      # If the memory_type is spad, Aladdin will initiate a 1-way cache for every
      # datapath, though this cache will not be used in simulation.
      # Since Ruby doesn't support 1-way cache, so set the assoc to 2.
      datapath.cacheAssoc = 2
    if (memory_type != "cache" and memory_type != "spad"):
      fatal("Aladdin configuration file specified invalid memory type %s for "
            "accelerator %s." % (memory_type, accel))
    datapaths.append(datapath)
  for datapath in datapaths:
    setattr(system, datapath.acceleratorName, datapath)

# Sanity check
if options.fastmem:
    if CPUClass != AtomicSimpleCPU:
        fatal("Fastmem can only be used with atomic CPU!")
    if (options.caches or options.l2cache):
        fatal("You cannot use fastmem in combination with caches!")

if options.simpoint_profile:
    if not options.fastmem:
        # Atomic CPU checked with fastmem option already
        fatal("SimPoint generation should be done with atomic cpu and fastmem")
    if np > 1:
        fatal("SimPoint generation not supported with more than one CPUs")

for i in xrange(np):
    if options.smt:
        system.cpu[i].workload = multiprocesses
    elif len(multiprocesses) == 1:
        system.cpu[i].workload = multiprocesses[0]
    else:
        system.cpu[i].workload = multiprocesses[i]

    if options.fastmem:
        system.cpu[i].fastmem = True

    if options.simpoint_profile:
        system.cpu[i].simpoint_profile = True
        system.cpu[i].simpoint_interval = options.simpoint_interval

    if options.checker:
        system.cpu[i].addCheckerCpu()

    system.cpu[i].createThreads()

if options.ruby:
    if not (options.cpu_type == "TimingSimpleCPU" or options.cpu_type == "DerivO3CPU"):
        print >> sys.stderr, "Ruby requires TimingSimpleCPU or DerivO3CPU!!"
        sys.exit(1)

    Ruby.create_system(options, False, system)
    assert(options.num_cpus + 2*len(system.find_all(HybridDatapath)[0]) ==
           len(system.ruby._cpu_ports))

    system.ruby.clk_domain = SrcClockDomain(clock = options.ruby_clock,
                                        voltage_domain = system.voltage_domain)
    for i in xrange(np):
        ruby_port = system.ruby._cpu_ports[i]

        # Create the interrupt controller and connect its ports to Ruby
        # Note that the interrupt controller is always present but only
        # in x86 does it have message ports that need to be connected
        system.cpu[i].createInterruptController()

        # Connect the cpu's cache ports to Ruby
        system.cpu[i].icache_port = ruby_port.slave
        system.cpu[i].dcache_port = ruby_port.slave
        if buildEnv['TARGET_ISA'] == 'x86':
            system.cpu[i].interrupts[0].pio = ruby_port.master
            system.cpu[i].interrupts[0].int_master = ruby_port.slave
            system.cpu[i].interrupts[0].int_slave = ruby_port.master
            system.cpu[i].itb.walker.port = ruby_port.slave
            system.cpu[i].dtb.walker.port = ruby_port.slave

    if options.accel_cfg_file:
        for i,datapath in enumerate(datapaths):
            datapath.cache_port = system.ruby._cpu_ports[options.num_cpus+2*i].slave
            datapath.spad_port = system.ruby._cpu_ports[options.num_cpus+2*i+1].slave

else:
    system.membus = SystemXBar(width=options.xbar_width)

    system.system_port = system.membus.slave
    CacheConfig.config_cache(options, system)
    MemConfig.config_mem(options, system)

root = Root(full_system = False, system = system)
Simulation.run(options, root, system, FutureClass)
