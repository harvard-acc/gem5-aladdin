#!/usr/bin/env python
# CortexSuite benchmark definitions

from design_sweep_types import *

disparity = Benchmark("disparity", "src/c/script_disparity")
disparity.set_kernels(["correlateSAD_2D", "computeSAD", "integralImage2D2D",
                       "finalSAD", "findDisparity"])
disparity.add_loop("computeSAD", 16)
disparity.add_loop("computeSAD", 18)
disparity.add_loop("integralImage2D2D", 16)
disparity.add_loop("integralImage2D2D", 19)
disparity.add_loop("integralImage2D2D", 20)
disparity.add_loop("integralImage2D2D", 25)
disparity.add_loop("integralImage2D2D", 26)
disparity.add_loop("correlateSAD_2D", 27)
disparity.add_loop("finalSAD", 18)
disparity.add_loop("finalSAD", 20)
disparity.add_loop("findDisparity", 16)
disparity.add_loop("findDisparity", 18)
disparity.generate_separate_kernels(separate=True, enforce_order=True)
disparity.use_local_makefile()

CORTEXSUITE = [disparity]
