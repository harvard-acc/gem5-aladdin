# Definitions of sweep parameters.

from xenon.base.datatypes import Param

BLOCK = "block"
CYCLIC = "cyclic"
COMPLETE = "complete"
NO_UNROLL = 1
FLATTEN = 0

def shortSizeToInt(size_str):
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

def intToShortSize(size):
  """ Go from 16384 -> 16kB. """
  if size < 1024:
    return str(size)
  else:
    return "%dkB" % (size/1024)

# Core Aladdin parameters.
cycle_time = Param("cycle_time", 1)
unrolling = Param("unrolling", 1)
partition_factor = Param("partition_factor", 1)
partition_type = Param("partition_type", CYCLIC)
# Aladdin currently only accepts integer values.
pipelining = Param("pipelining", False, format_func=lambda b: str(int(b)))

# Cache memory system parameters.
cache_size = Param("cache_size", 16384, format_func=intToShortSize)
cache_assoc = Param("cache_assoc", 4)
cache_hit_latency = Param("cache_hit_latency", 1)
cache_line_sz = Param("cache_line_sz", 32)
tlb_hit_latency = Param("tlb_hit_latency", 0)
tlb_miss_latency = Param("tlb_miss_latency", 20)
tlb_page_size = Param("tlb_page_size", 4096)
tlb_entries = Param("tlb_entries", 0)
tlb_max_outstanding_walks = Param("tlb_max_outstanding_walks", 0)
tlb_assoc = Param("tlb_assoc", 0)
tlb_bandwidth = Param("tlb_bandwidth", 1)
load_bandwidth = Param("load_bandwidth", 1)
store_bandwidth = Param("store_bandwidth", 1)
l2cache_size = Param("l2cache_size", 128*1024, format_func=intToShortSize)
perfect_l1 = Param("perfect_l1", False)
perfect_bus = Param("perfect_bus", False)
enable_l2 = Param("enable_l2", False)

# DMA settings.
dma_setup_latency = Param("dma_setup_latency", 40)
max_dma_requests = Param("max_dma_requests", 40)
dma_chunk_size = Param("dma_chunk_size", 64)
pipelined_dma = Param("pipelined_dma", 0)
ready_mode = Param("ready_mode", 0)
dma_multi_channel = Param("dma_multi_channel", 0)
ignore_cache_flush = Param("ignore_cache_flush", 0)
