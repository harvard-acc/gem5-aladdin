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

from __future__ import print_function

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
from common import CpuConfig
from common import MemConfig
from common.FileSystemConfig import config_filesystem

from common.Caches import *
from common.cpu2000 import *

def addAladdinOptions(parser):
    parser.add_option("--accel_cfg_file", default=None,
        help="Aladdin accelerator configuration file.")
    parser.add_option("--aladdin-debugger", action="store_true",
        help="Run the Aladdin debugger on accelerator initialization.")

def createAladdinDatapath(config, accel):
    memory_type = config.get(accel, 'memory_type').lower()
    # Accelerators need their own clock domain!
    cycleTime = config.getint(accel, "cycle_time")
    clock = "%1.3fGHz" % (1.0/cycleTime)
    clk_domain = SrcClockDomain(
        clock = clock, voltage_domain = system.cpu_voltage_domain)
    # Set the globally required parameters.
    datapath = HybridDatapath(
        clk_domain = clk_domain,
        benchName = accel,
        # TODO: Ideally bench_name would change to output_prefix but that's
        # a pretty big breaking change.
        outputPrefix = config.get(accel, "bench_name"),
        traceFileName = config.get(accel, "trace_file_name"),
        configFileName = config.get(accel, "config_file_name"),
        acceleratorName = "%s_datapath" % accel,
        acceleratorId = config.getint(accel, "accelerator_id"),
        cycleTime = cycleTime,
        useDb = config.getboolean(accel, "use_db"),
        experimentName = config.get(accel, "experiment_name"),
        enableStatsDump = options.enable_stats_dump_and_resume)
    datapath.cacheLineFlushLatency = config.getint(accel,
        "cacheline_flush_latency")
    datapath.cacheLineInvalidateLatency = config.getint(accel,
        "cacheline_invalidate_latency")
    datapath.dmaSetupOverhead = config.getint(accel, "dma_setup_overhead")
    datapath.maxDmaRequests = config.getint(accel, "max_dma_requests")
    datapath.numDmaChannels = config.getint(accel, "num_dma_channels")
    datapath.dmaChunkSize = config.getint(accel, "dma_chunk_size")
    datapath.pipelinedDma = config.getboolean(accel, "pipelined_dma")
    datapath.ignoreCacheFlush = config.getboolean(accel,
        "ignore_cache_flush")
    datapath.invalidateOnDmaStore = config.getboolean(accel,
        "invalidate_on_dma_store")
    datapath.recordMemoryTrace = config.getboolean(accel,
        "record_memory_trace")
    datapath.enableAcp = config.getboolean(accel, "enable_acp")
    datapath.useAcpCache = True
    datapath.acpCacheSize = config.get(accel, "acp_cache_size")
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
        # If the memory_type is spad, Aladdin will initialize a 2-way cache
        # for every datapath, although this cache will not be used in
        # simulation. Ruby doesn't support direct-mapped caches, so set the
        # assoc to 2.
        datapath.cacheAssoc = 2
    if (memory_type != "cache" and memory_type != "spad"):
        fatal("Aladdin configuration file specified invalid memory type %s "
              "for accelerator %s." % (memory_type, accel))
    setattr(system, datapath.acceleratorName, datapath)

def createSystolicArrayDatapath(config, accel):
    acceleratorId = config.getint(accel, "accelerator_id")
    peArrayRows = config.getint(accel, "pe_array_rows")
    peArrayCols = config.getint(accel, "pe_array_cols")
    dataType = config.get(accel, "data_type")
    sramSize = config.getint(accel, "sram_size")
    lineSize = config.getint(accel, "line_size")
    fetchQueueCapacity = config.getint(accel, "fetch_queue_capacity")
    commitQueueCapacity = config.getint(accel, "commit_queue_capacity")
    numSpadBanks = config.getint(accel, "num_spad_banks")
    numSpadPorts = config.getint(accel, "num_spad_ports")
    partType = config.get(accel, "partition_type")
    # Set the globally required parameters.
    datapath = SystolicArray(
        acceleratorName = accel,
        acceleratorId = acceleratorId,
        peArrayRows = peArrayRows,
        peArrayCols = peArrayCols,
        dataType = dataType,
        lineSize = lineSize,
        fetchQueueCapacity = fetchQueueCapacity,
        commitQueueCapacity = commitQueueCapacity,
        inputSpad = Scratchpad(
            size = sramSize,
            lineSize = lineSize,
            numBanks = numSpadBanks,
            numPorts = numSpadPorts,
            partType = partType),
        weightSpad = Scratchpad(
            size = sramSize,
            lineSize = lineSize,
            numBanks = numSpadBanks,
            numPorts = numSpadPorts,
            partType = partType),
        outputSpad = Scratchpad(
            size = sramSize,
            lineSize = lineSize,
            numBanks = numSpadBanks,
            numPorts = numSpadPorts,
            partType = partType))
    # Attach the scratchpads and the fetch/commit units of the accelerator
    # to the bus.
    # Input scratchpad.
    for i in xrange(peArrayRows):
        datapath.input_spad_port[i] = datapath.inputSpadBus.slave
    datapath.inputSpadBus.master = datapath.inputSpad.accelSidePort
    # Weight scratchpad.
    for i in xrange(peArrayCols):
        datapath.weight_spad_port[i] = datapath.weightSpadBus.slave
    datapath.weightSpadBus.master = datapath.weightSpad.accelSidePort
    # Output scratchpad.
    for i in xrange(peArrayRows):
        datapath.output_spad_port[i] = datapath.outputSpadBus.slave
    datapath.outputSpadBus.master = datapath.outputSpad.accelSidePort

    setattr(system, datapath.acceleratorName, datapath)

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
        process = Process(pid = 100 + idx)
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
        assert(options.cpu_type == "DerivO3CPU")
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
    print("Error: script doesn't take any positional arguments")
    sys.exit(1)

multiprocesses = []
numThreads = 1

np = options.num_cpus
if np > 0:
  if options.bench:
      apps = options.bench.split("-")
      if len(apps) != options.num_cpus:
          print("number of benchmarks not equal to set num_cpus!")
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
CPUClass.numThreads = numThreads

MemClass = Simulation.setMemClass(options)

# Check -- do not allow SMT with multiple CPUs
if options.smt and options.num_cpus > 1:
    fatal("You cannot use SMT with multiple CPUs!")

system = System(cpu = [CPUClass(cpu_id=i) for i in range(np)],
                mem_mode = test_mem_mode,
                mem_ranges = [AddrRange(options.mem_size)],
                cache_line_size = options.cacheline_size)

if numThreads > 1:
    system.multi_thread = True

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

# If elastic tracing is enabled, then configure the cpu and attach the elastic
# trace probe
if options.elastic_trace_en:
    CpuConfig.config_etrace(CPUClass, system.cpu, options)

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
    for accel in accels:
        acceleratorType = config.get(accel, "accelerator_type")
        if (acceleratorType == "aladdin"):
            createAladdinDatapath(config, accel)
        elif (acceleratorType == "systolic_array"):
            createSystolicArrayDatapath(config, accel)
        else:
            fatal("Unknown accelerator type %s!" % acceleratorType)

if options.simpoint_profile:
    if not CpuConfig.is_atomic_cpu(TestCPUClass):
        fatal("SimPoint/BPProbe should be done with an atomic cpu")

for i in xrange(np):
    if options.smt:
        system.cpu[i].workload = multiprocesses
    elif len(multiprocesses) == 1:
        system.cpu[i].workload = multiprocesses[0]
    else:
        # If the number of CPUs is greater than the number of processes,
        # assign the first workload to the remaining CPUs (as the condition
        # above us did).
        if i >= len(multiprocesses):
            system.cpu[i].workload = multiprocesses[0]
        else:
            system.cpu[i].workload = multiprocesses[i]

    if options.simpoint_profile:
        system.cpu[i].addSimPointProbe(options.simpoint_interval)

    if options.checker:
        system.cpu[i].addCheckerCpu()

    if options.bp_type:
        bpClass = ObjectList.bp_list.get(options.bp_type)
        system.cpu[i].branchPred = bpClass()

    if options.indirect_bp_type:
        indirectBPClass = \
            ObjectList.indirect_bp_list.get(options.indirect_bp_type)
        system.cpu[i].branchPred.indirectBranchPred = indirectBPClass()

    system.cpu[i].createThreads()

if options.ruby:
    Ruby.create_system(options, False, system)
    assert(options.num_cpus + 3*len(system.find_all(HybridDatapath)[0] + \
           system.find_all(SystolicArray)[0]) == len(system.ruby._cpu_ports))

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
        datapaths = []
        datapaths.extend(system.find_all(HybridDatapath)[0])
        datapaths.extend(system.find_all(SystolicArray)[0])
        for i,datapath in enumerate(datapaths):
            datapath.cache_port = system.ruby._cpu_ports[options.num_cpus+3*i].slave
            datapath.spad_port = system.ruby._cpu_ports[options.num_cpus+3*i+1].slave
            datapath.acp_port = system.ruby._cpu_ports[options.num_cpus+3*i+2].slave

else:
    system.membus = SystemXBar(width=options.xbar_width)

    system.system_port = system.membus.slave
    CacheConfig.config_cache(options, system)
    MemConfig.config_mem(options, system)
    config_filesystem(system, options)

root = Root(full_system = False, system = system)
Simulation.run(options, root, system, FutureClass)
