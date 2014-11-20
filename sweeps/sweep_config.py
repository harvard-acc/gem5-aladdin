#!/usr/bin/env python
# Design sweep definition. Import this file at the top of generate_design_sweep.

from design_sweep_types import *

# Sweep parameters. If a certain parameter should not be swept, set the value of
# step to NO_SWEEP, and the value of start will be used as a constant. end will
# be ignored. step should never be less than 1.
unrolling = SweepParam(
    "unrolling", start=1, end=32, step=2, step_type=EXP_SWEEP)
partition = SweepParam(
    "partition", start=1, end=32, step=2, step_type=EXP_SWEEP)
pipelining = SweepParam(
    "pipelining", start=0, end=1, step=1, step_type=LINEAR_SWEEP)
tlb_entries = SweepParam(
    "tlb_entries", start=1, end=8, step=2, step_type=EXP_SWEEP)
tlb_max_outstanding_walks = SweepParam(
    "tlb_max_outstanding_walks", start=2, end=2, step=1, step_type=LINEAR_SWEEP)
load_bandwidth = SweepParam(
    "load_bandwidth", start=2, end=2, step=2, step_type=EXP_SWEEP)
store_bandwidth = SweepParam(
    "store_bandwidth", start=1, end=8, step=2, step_type=EXP_SWEEP)
load_queue_size = SweepParam(
    "load_queue_size", start=8, end=32, step=2, step_type=EXP_SWEEP)
store_queue_size = SweepParam(
    "store_queue_size", start=8, end=32, step=2, step_type=EXP_SWEEP)
cache_size = SweepParam(
     "cache_size", start=16384, end=131072, step=2, step_type=EXP_SWEEP)
cache_hit_latency = SweepParam(
    "cache_hit_latency", start=1, end=1, step=1, step_type=LINEAR_SWEEP)
cache_assoc = SweepParam(
    "cache_assoc", start=8, end=8, step=2, step_type=EXP_SWEEP)
cache_line_sz = SweepParam(
    "cache_line_sz", start=64, end=64, step=2, step_type=EXP_SWEEP)
dma_setup_latency = SweepParam(
    "dma_setup_latency", start=1, end=1, step=1, step_type=LINEAR_SWEEP)
