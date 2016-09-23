# CortexSuite benchmark definitions.

from datatypes import *
from params import *

disparity = Benchmark("disparity", "disparity/src/c")
disparity.set_kernels(
    ["computeSAD",
     "integralImage2D2D",
     "finalSAD",
     "findDisparity"])
# computeSAD
disparity.set_main_id(0x1f0)
disparity.add_loop("computeSAD", "outer")
disparity.add_loop("computeSAD", "inner")
disparity.add_array("Ileft", 29810, 4)
disparity.add_array("Iright_moved", 29810, 4)
disparity.add_array("SAD", 29810, 4)
# integralImage2D2D
disparity.add_loop("integralImage2D2D", "loop1")
disparity.add_loop("integralImage2D2D", "loop2_outer")
disparity.add_loop("integralImage2D2D", "loop2_inner")
disparity.add_loop("integralImage2D2D", "loop3_outer")
disparity.add_loop("integralImage2D2D", "loop3_inner")
disparity.add_array("integralImg", 29810, 4)
# finalSAD
disparity.add_loop("finalSAD", "outer")
disparity.add_loop("finalSAD", "inner")
disparity.add_array("retSAD", 27106, 4)
# findDisparity
disparity.add_loop("findDisparity", "outer")
disparity.add_loop("findDisparity", "inner")
disparity.add_array("minSAD", 27106, 4)
disparity.add_array("retDisp", 27106, 4)
disparity.add_required_files(["../../data/qcif/1.bmp", "../../data/qcif/2.bmp"])
disparity.set_exec_cmd("disparity-gem5-accel")
disparity.set_run_args(".")
