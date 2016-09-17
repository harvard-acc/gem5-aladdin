# Definitions of sweep parameters.

from xenon.base.datatypes import Param

BLOCK = "block"
CYCLIC = "cyclic"
COMPLETE = "complete"
NO_UNROLL = 1
FLATTEN = 0

# Core Aladdin parameters.
cycle_time = Param("cycle_time", 1)
unrolling = Param("unrolling", 1)
partition_factor = Param("partition_factor", 1)
partition_type = Param("partition_type", CYCLIC)
pipelining = Param("pipelining", False)

# Cache memory system parameters.
cache_size = Param("cache_size", 16*1024)
cache_assoc = Param("cache_assoc", 4)
cache_hit_latency = Param("cache_hit_latency", 1)
cache_line_sz = Param("cache_line_sz", 32)
tlb_hit_latency = Param("tlb_hit_latency", 0),
tlb_miss_latency = Param("tlb_miss_latency", 20)
tlb_page_size = Param("tlb_page_size", 4096)
tlb_entries = Param("tlb_entries", 0)
tlb_max_outstanding_walks = Param("tlb_max_outstanding_walks", 0)
tlb_assoc = Param("tlb_assoc", 0)
tlb_bandwidth = Param("tlb_bandwidth", 1)
load_bandwidth = Param("load_bandwidth", 1)
store_bandwidth = Param("store_bandwidth", 1)
l2cache_size = Param("l2cache_size", 128*1024)

# DMA optimizations
pipelined_dma = Param("pipelined_dma", 0)
ready_mode = Param("ready_mode", 0)
dma_multi_channel = Param("dma_multi_channel", 0)
ignore_cache_flush = Param("ignore_cache_flush", 0)
