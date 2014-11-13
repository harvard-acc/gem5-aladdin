#!/usr/bin/env python
# SHOC design sweep definition. Import this file at the top of
# generate_design_sweep.

from design_sweep_types import *

# Sweep parameters. If a certain parameter should not be swept, set the value of
# step to NO_SWEEP, and the value of start will be used as a constant. end will
# be ignored. step should never be less than 1.
cache_size = SweepParam(
     "cache_size", start=16384, end=16384, step=NO_SWEEP, step_type=EXP_SWEEP)
cache_assoc = SweepParam(
    "cache_assoc", start=1, end=1, step=1, step_type=LINEAR_SWEEP)
pipelining = SweepParam(
    "pipelining", start=1, end=1, step=1, step_type=LINEAR_SWEEP)
unrolling = SweepParam(
    "unrolling", start=1, end=64, step=2, step_type=EXP_SWEEP)
partition = SweepParam(
    "partition", start=1, end=64, step=2, step_type=EXP_SWEEP)
cache_hit_latency = SweepParam(
    "cache_hit_latency", start=1, end=1, step=1, step_type=LINEAR_SWEEP)
tlb_entries = SweepParam(
    "tlb_entries", start=32, end=32, step=2, step_type=EXP_SWEEP)
tlb_max_outstanding_walks = SweepParam(
    "tlb_max_outstanding_walks", start=2, end=2, step=1, step_type=LINEAR_SWEEP)
load_bandwidth = SweepParam(
    "load_bandwidth", start=2, end=2, step=1, step_type=LINEAR_SWEEP)
store_bandwidth = SweepParam(
    "store_bandwidth", start=2, end=2, step=1, step_type=LINEAR_SWEEP)
store_queue_size = SweepParam(
    "store_queue_size", start=8, end=8, step=8, step_type=LINEAR_SWEEP)
load_queue_size = SweepParam(
    "store_queue_size", start=8, end=8, step=8, step_type=LINEAR_SWEEP)

# Benchmarks

bb_gemm = Benchmark("bb_gemm", "bb_gemm")
bb_gemm.set_kernels(["bb_gemm"])
bb_gemm.add_array("x", 1024, 4, PARTITION_CYCLIC)
bb_gemm.add_array("y", 1024, 4, PARTITION_CYCLIC)
bb_gemm.add_array("z", 1024, 4, PARTITION_CYCLIC)
bb_gemm.add_loop("bb_gemm", 5)
bb_gemm.add_loop("bb_gemm", 6, UNROLL_FLATTEN)
bb_gemm.add_loop("bb_gemm", 8, UNROLL_FLATTEN)

fft = Benchmark("fft", "fft")
fft.set_kernels([
    "fft1D_512", "step1", "step2", "step3", "step4", "step5", "step6",
    "step7", "step8", "step9", "step10", "step11"])
fft.add_array("work_x", 512, 4, PARTITION_CYCLIC)
fft.add_array("work_y", 512, 4, PARTITION_CYCLIC)
fft.add_array("DATA_x", 512, 4, PARTITION_CYCLIC)
fft.add_array("DATA_y", 512, 4, PARTITION_CYCLIC)
fft.add_array("data_x", 8, 4, PARTITION_COMPLETE)
fft.add_array("data_y", 8, 4, PARTITION_COMPLETE)
fft.add_array("smem", 576, 4, PARTITION_CYCLIC)
fft.add_array("reversed", 8, 4, PARTITION_COMPLETE)
fft.add_array("sin_64", 484, 4, PARTITION_COMPLETE)
fft.add_array("cos_64", 484, 4, PARTITION_COMPLETE)
fft.add_array("sin_484", 484, 4, PARTITION_COMPLETE)
fft.add_array("cos_484", 484, 4, PARTITION_COMPLETE)
fft.add_loop("step1", 16)
fft.add_loop("step1", 18, UNROLL_FLATTEN)
fft.add_loop("step1", 26, UNROLL_FLATTEN)
fft.add_loop("step1", 36, UNROLL_FLATTEN)
fft.add_loop("step2", 53)
fft.add_loop("step2", 54, UNROLL_FLATTEN)
fft.add_loop("step3", 75)
fft.add_loop("step3", 76, UNROLL_FLATTEN)
fft.add_loop("step3", 83, UNROLL_FLATTEN)
fft.add_loop("step4", 100)
fft.add_loop("step4", 101, UNROLL_FLATTEN)
fft.add_loop("step5", 122)
fft.add_loop("step5", 123, UNROLL_FLATTEN)
fft.add_loop("step5", 130, UNROLL_FLATTEN)
fft.add_loop("step6", 149)
fft.add_loop("step6", 151, UNROLL_FLATTEN)
fft.add_loop("step6", 164, UNROLL_FLATTEN)
fft.add_loop("step6", 174, UNROLL_FLATTEN)
fft.add_loop("step7", 193)
fft.add_loop("step7", 194, UNROLL_FLATTEN)
fft.add_loop("step8", 216)
fft.add_loop("step8", 217, UNROLL_FLATTEN)
fft.add_loop("step8", 224, UNROLL_FLATTEN)
fft.add_loop("step9", 243)
fft.add_loop("step9", 244, UNROLL_FLATTEN)
fft.add_loop("step10", 266)
fft.add_loop("step10", 267, UNROLL_FLATTEN)
fft.add_loop("step10", 274, UNROLL_FLATTEN)
fft.add_loop("step11", 293)
fft.add_loop("step11", 295, UNROLL_FLATTEN)
fft.add_loop("step11", 304, UNROLL_FLATTEN)

md = Benchmark("md", "md")
md.set_kernels(["md", "md_kernel"])
md.add_array("d_force_x", 32, 4, PARTITION_CYCLIC)
md.add_array("d_force_y", 32, 4, PARTITION_CYCLIC)
md.add_array("d_force_z", 32, 4, PARTITION_CYCLIC)
md.add_array("position_x", 32, 4, PARTITION_CYCLIC)
md.add_array("position_y", 32, 4, PARTITION_CYCLIC)
md.add_array("position_z", 32, 4, PARTITION_CYCLIC)
md.add_array("NL", 1024, 4, PARTITION_CYCLIC)
md.add_loop("md_kernel", 19)

pp_scan = Benchmark("pp_scan", "pp_scan")
pp_scan.set_kernels(["pp_scan", "local_scan", "sum_scan", "last_step_scan"])
pp_scan.add_array("bucket", 2048, 4, PARTITION_CYCLIC)
pp_scan.add_array("bucket2", 2048, 4, PARTITION_CYCLIC)
pp_scan.add_array("sum", 16, 4, PARTITION_CYCLIC)
pp_scan.add_loop("sum_scan", 26)
pp_scan.add_loop("local_scan", 15)
pp_scan.add_loop("local_scan", 16, UNROLL_FLATTEN)
pp_scan.add_loop("last_step_scan", 33)
pp_scan.add_loop("last_step_scan", 34, UNROLL_FLATTEN)

reduction = Benchmark("reduction", "reduction")
reduction.set_kernels(["reduction"])
reduction.add_array("in", 2048, 4, PARTITION_CYCLIC)
reduction.add_loop("reduction", 8)

ss_sort = Benchmark("ss_sort", "ss_sort")
ss_sort.set_kernels(["ss_sort", "init", "hist", "local_scan", "sum_scan",
                     "last_step_scan", "update"])
ss_sort.add_array("a", 2048, 4, PARTITION_CYCLIC)
ss_sort.add_array("b", 2048, 4, PARTITION_CYCLIC)
ss_sort.add_array("bucket", 2048, 4, PARTITION_CYCLIC)
ss_sort.add_array("sum", 128, 4, PARTITION_CYCLIC)
ss_sort.add_loop("init", 52)
ss_sort.add_loop("hist", 61)
ss_sort.add_loop("hist", 63, UNROLL_FLATTEN)
ss_sort.add_loop("local_scan", 17)
ss_sort.add_loop("local_scan", 19, UNROLL_FLATTEN)
ss_sort.add_loop("sum_scan", 30)
ss_sort.add_loop("last_step_scan", 38)
ss_sort.add_loop("last_step_scan", 40, UNROLL_FLATTEN)
ss_sort.add_loop("update", 75)
ss_sort.add_loop("update", 77, UNROLL_FLATTEN)

stencil = Benchmark("stencil", "stencil")
stencil.set_kernels(["stencil"])
stencil.add_array("orig", 1156, 4, PARTITION_CYCLIC)
stencil.add_array("sol", 1156, 4, PARTITION_CYCLIC)
stencil.add_array("filter", 9, 4, PARTITION_CYCLIC)
stencil.add_loop("stencil", 11)
stencil.add_loop("stencil", 12, UNROLL_FLATTEN)

triad = Benchmark("triad", "triad")
triad.set_kernels(["triad"])
triad.add_loop("triad", 5)
triad.add_array("a", 2048, 4, PARTITION_CYCLIC)
triad.add_array("b", 2048, 4, PARTITION_CYCLIC)
triad.add_array("c", 2048, 4, PARTITION_CYCLIC)

SHOC = [bb_gemm, fft, md, pp_scan, reduction, ss_sort, stencil, triad]
