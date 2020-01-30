# Copyright (c) 2006-2007 The Regents of The University of Michigan
# Copyright (c) 2009 Advanced Micro Devices, Inc.
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
# Authors: Brad Beckmann

import math
import m5
from m5.objects import *
from m5.defines import buildEnv
from Ruby import create_topology
from Ruby import send_evicts

#
# Declare caches used by the protocol
#
class L1Cache(RubyCache): pass
class L2Cache(RubyCache): pass

def define_options(parser):
    return

def create_system(options, full_system, system, dma_ports, bootmem,
                  ruby_system):

    if buildEnv['PROTOCOL'] != 'MESI_Two_Level_aladdin':
        fatal("This script requires the MESI_Two_Level_aladdin protocol to be \
            built.")

    # Run the original protocol script
    buildEnv['PROTOCOL'] = buildEnv['PROTOCOL'][:-8]
    protocol = buildEnv['PROTOCOL']
    exec "import %s" % protocol
    try:
        (cpu_sequencers, dir_cntrls, topology) = \
            eval("%s.create_system(options, full_system, system, dma_ports, \
                bootmem, ruby_system)" % protocol)
    except:
        print "Error: could not create system for ruby protocol inside fusion \
            system %s" % protocol
        raise

    #
    # Must create the individual controllers before the network to ensure the
    # controller constructors are called before the network constructor
    #
    l2_bits = int(math.log(options.num_l2caches, 2))
    block_size_bits = int(math.log(options.cacheline_size, 2))

    #
    # Build accelerator
    #
    # Accelerator cache
    datapaths = system.find_all(HybridDatapath)[0]
    datapaths.extend(system.find_all(SystolicArray)[0])
    for i,datapath in enumerate(datapaths):
        l1i_cache = L1Cache(size = '256B',
                            assoc = 2,
                            start_index_bit = block_size_bits,
                            is_icache = True)
        # accelerator's private cache
        l1d_cache = L1Cache(size = datapath.cacheSize,
                            assoc = datapath.cacheAssoc,
                            start_index_bit = block_size_bits,
                            is_icache = False)

        prefetcher = RubyPrefetcher.Prefetcher()

        l1_cntrl = L1Cache_Controller(version = options.num_cpus+i,
                                      L1Icache = l1i_cache,
                                      L1Dcache = l1d_cache,
                                      l2_select_num_bits = l2_bits,
                                      send_evictions = send_evicts(options),
                                      prefetcher = prefetcher,
                                      ruby_system = ruby_system,
                                      #clk_domain = clk_domain,
                                      #transitions_per_cycle = options.ports,
                                      enable_prefetch = False)

        acc_seq = RubySequencer(version = options.num_cpus+i,
                                icache = l1i_cache,
                                dcache = l1d_cache,
                                #clk_domain = clk_domain,
                                ruby_system = ruby_system)

        l1_cntrl.sequencer = acc_seq
        setattr(ruby_system, "l1_cntrl_acc%d" % i, l1_cntrl)

        # Add controllers and sequencers to the appropriate lists
        cpu_sequencers.append(acc_seq)
        topology.addController(l1_cntrl)

        # Connect the L1 controllers and the network
        l1_cntrl.mandatoryQueue = MessageBuffer()
        l1_cntrl.requestFromL1Cache = MessageBuffer()
        l1_cntrl.requestFromL1Cache.master = ruby_system.network.slave
        l1_cntrl.responseFromL1Cache = MessageBuffer()
        l1_cntrl.responseFromL1Cache.master = ruby_system.network.slave
        l1_cntrl.unblockFromL1Cache = MessageBuffer()
        l1_cntrl.unblockFromL1Cache.master = ruby_system.network.slave

        l1_cntrl.optionalQueue = MessageBuffer()

        l1_cntrl.requestToL1Cache = MessageBuffer()
        l1_cntrl.requestToL1Cache.slave = ruby_system.network.master
        l1_cntrl.responseToL1Cache = MessageBuffer()
        l1_cntrl.responseToL1Cache.slave = ruby_system.network.master

        # Scratchpad port
        # The scratchpad port is conneted to the DMA controller
        spad_seq = DMASequencer(version = i,
                                ruby_system = ruby_system)

        spad_cntrl = DMA_Controller(version = i, dma_sequencer = spad_seq,
                                    transitions_per_cycle = options.ports,
                                    ruby_system = ruby_system)

        setattr(ruby_system, "spad_cntrl_acc%d" % i, spad_cntrl)

        # Connect the dma controllers and the network
        spad_cntrl.mandatoryQueue = MessageBuffer()
        spad_cntrl.requestToDir = MessageBuffer()
        spad_cntrl.requestToDir.master = ruby_system.network.slave
        spad_cntrl.responseFromDir = MessageBuffer(ordered = True)
        spad_cntrl.responseFromDir.slave = ruby_system.network.master

        cpu_sequencers.append(spad_seq)
        topology.addController(spad_cntrl)

        # ACP port
        acp_dummy_ic = L1Cache(size = '256B',
                               assoc = 2,
                               start_index_bit = block_size_bits,
                               is_icache = True)
        acp_dummy_dc = L1Cache(size = '256B',
                               assoc = 2,
                               start_index_bit = block_size_bits,
                               is_icache = False)
        acp_cntrl = ACP_Controller(version = i,
                                   l2_select_num_bits = l2_bits,
                                   write_through = True,
                                   ruby_system = ruby_system)

        acp_seq = RubySequencer(version = i,
                                icache = acp_dummy_ic,
                                dcache = acp_dummy_dc,
                                #clk_domain = clk_domain,
                                #transitions_per_cycle = options.ports,
                                ruby_system = ruby_system)

        acp_cntrl.sequencer = acp_seq
        setattr(ruby_system, "acp_cntrl_acc%d" % i, acp_cntrl)

        # Add controllers and sequencers to the appropriate lists
        cpu_sequencers.append(acp_seq)
        topology.addController(acp_cntrl)

        # Connect the ACP controller and the network
        acp_cntrl.mandatoryQueue = MessageBuffer()
        acp_cntrl.requestToL2 = MessageBuffer()
        acp_cntrl.requestToL2.master = ruby_system.network.slave
        acp_cntrl.responseFromL2 = MessageBuffer(ordered = True)
        acp_cntrl.responseFromL2.slave = ruby_system.network.master

    return (cpu_sequencers, dir_cntrls, topology)
