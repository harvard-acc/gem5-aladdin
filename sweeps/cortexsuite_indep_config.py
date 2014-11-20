#!/usr/bin/env python
# CortexSuite benchmark definitions, treating each kernel as its own benchmark

from design_sweep_types import *

computeSAD = Benchmark("computeSAD", "disparity/src/c/computeSAD")
computeSAD.set_kernels(["computeSAD"])
computeSAD.add_loop("computeSAD", 16)
computeSAD.add_loop("computeSAD", 18)
computeSAD.add_array("Ileft", 27770, 4, PARTITION_CYCLIC)
computeSAD.add_array("Iright_moved", 27770, 4, PARTITION_CYCLIC)
computeSAD.add_array("SAD", 27770, 4, PARTITION_CYCLIC)
computeSAD.use_local_makefile()

integralImage2D2D = Benchmark("integralImage2D2D",
                              "disparity/src/c/integralImage2D2D")
integralImage2D2D.set_kernels(["integralImage2D2D"])
integralImage2D2D.add_loop("integralImage2D2D", 16)
integralImage2D2D.add_loop("integralImage2D2D", 19)
integralImage2D2D.add_loop("integralImage2D2D", 20)
integralImage2D2D.add_loop("integralImage2D2D", 25)
integralImage2D2D.add_loop("integralImage2D2D", 26)
integralImage2D2D.add_array("SAD", 27770, 4, PARTITION_CYCLIC)
integralImage2D2D.add_array("integralImg", 27770, 4, PARTITION_CYCLIC)
integralImage2D2D.use_local_makefile()

correlateSAD_2D = Benchmark("correlateSAD_2D",
                            "disparity/src/c/correlateSAD_2D")
correlateSAD_2D.set_kernels(["correlateSAD_2D"])
correlateSAD_2D.add_loop("correlateSAD_2D", 27)
correlateSAD_2D.add_array("Ileft", 27770, 4, PARTITION_CYCLIC)
correlateSAD_2D.add_array("Iright_moved", 27770, 4, PARTITION_CYCLIC)
correlateSAD_2D.add_array("SAD", 27770, 4, PARTITION_CYCLIC)
correlateSAD_2D.add_array("integralImg", 27770, 4, PARTITION_CYCLIC)
correlateSAD_2D.add_array("retSAD", 27106, 4, PARTITION_CYCLIC)
correlateSAD_2D.use_local_makefile()

finalSAD = Benchmark("finalSAD", "disparity/src/c/finalSAD")
finalSAD.set_kernels(["finalSAD"])
finalSAD.add_loop("finalSAD", 18)
finalSAD.add_loop("finalSAD", 20)
finalSAD.add_array("integralImg", 27770, 4, PARTITION_CYCLIC)
finalSAD.add_array("retSAD", 27106, 4, PARTITION_CYCLIC)
finalSAD.use_local_makefile()

findDisparity = Benchmark("findDisparity", "disparity/src/c/findDisparity")
findDisparity.set_kernels(["findDisparity"])
findDisparity.add_loop("findDisparity", 16)
findDisparity.add_loop("findDisparity", 18)
findDisparity.add_array("retSAD", 27106, 4, PARTITION_CYCLIC)
findDisparity.add_array("minSAD", 27106, 4, PARTITION_CYCLIC)
findDisparity.add_array("retDisp", 27106, 4, PARTITION_CYCLIC)
findDisparity.use_local_makefile()

CORTEXSUITE_KERNELS = [computeSAD, integralImage2D2D, correlateSAD_2D,
                       finalSAD, findDisparity]
