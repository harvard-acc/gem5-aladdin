#!/usr/bin/env bash

bmk_home=./
gem5_dir=../../../

${gem5_dir}/build/X86/gem5.opt \
  --debug-flags=Aladdin,SystolicToplevel,SystolicDataflow,SystolicFetch,SystolicCommit,SystolicPE,SystolicSpad \
  --outdir=${bmk_home}/outputs \
  ${gem5_dir}/configs/aladdin/aladdin_se.py \
  --num-cpus=1 \
  --enable_prefetchers \
  --mem-size=4GB \
  --mem-type=LPDDR4_3200_2x16  \
  --sys-clock=1GHz \
  --cpu-type=TimingSimpleCPU \
  --caches \
  --l2cache \
  --l2_size=2MB \
  --l1d_size=65536 \
  --l1i_size=65536 \
  --l2_assoc=16 \
  --l2_hit_latency=20 \
  --cacheline_size=32 \
  --accel_cfg_file=systolic_array.cfg \
  -c ${bmk_home}/test-gem5-accel \
  > stdout 2> stderr
