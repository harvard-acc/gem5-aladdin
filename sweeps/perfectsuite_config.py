#!/usr/bin/env python
# PERFECT-Suite benchmark definitions

from design_sweep_types import *

lucas_kanade = Benchmark("lucas-kanade", "wami/kernels/ser/lucas-kanade/src/main")
lucas_kanade.set_kernels(["steepest_descent_shadow"])
lucas_kanade.add_loop("steepest_descent_shadow", 90, UNROLL_ONE)
lucas_kanade.add_loop("steepest_descent_shadow", 91)
lucas_kanade.add_loop("steepest_descent_shadow", 102, UNROLL_FLATTEN)
lucas_kanade.add_array("gradX_warped", 101736, 4, PARTITION_CYCLIC, CACHE)
lucas_kanade.add_array("gradY_warped", 101736, 4, PARTITION_CYCLIC, CACHE)
lucas_kanade.add_array("changeset", 25344, 4, PARTITION_CYCLIC)
lucas_kanade.add_array("I_steepest", 608256, 4, PARTITION_CYCLIC, CACHE)
lucas_kanade.add_array("Jacobian_x", 24, 4, PARTITION_COMPLETE)
lucas_kanade.add_array("Jacobian_y", 24, 4, PARTITION_COMPLETE)
lucas_kanade.set_exec_cmd(
    "%(source_dir)s/wami/kernels/ser/lucas-kanade/lucas-kanade-gem5")
lucas_kanade.set_run_args(
    "/group/vlsiarch/samxi/active_projects/cortexsuite/vision"
    "/benchmarks/tracking/data/qcif frame .mat 1 10 --shadow --blocked")
lucas_kanade.set_main_id(0x00000290)
lucas_kanade.use_local_makefile()

PERFECTSUITE = [lucas_kanade]
