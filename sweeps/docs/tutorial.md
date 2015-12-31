Tutorial
--------

We'll start by running the design sweep generator script on a provided
benchmark suite, MachSuite.  These are the steps to generating and running a
design sweep:

1. Choose the simulation mode.
2. Define the design sweep parameters (sweep what from where to where).
3. Generate all necessary configuration files.
4. Generate all dynamic traces.
5. Run the simulations.

### Choose the simulation mode ###

  We can run Aladdin experiments in three different modes:

  1. **Aladdin standalone**

      In this mode, the accelerator is simulated entirely on its own. All memory
      is private scratchpad space. This is just stock Aladdin; in this mode,
      gem5 is not used at all.

  2. **gem5-cache**

      In this mode, the accelerator is attached to a memory hierarchy modeled by
      gem5. The memory system contains a private L1 dcache, an optional L2
      cache, and main memory. You decide whether an array in your accelerator
      should be mapped to private scratchpads or caches. There is no CPU. This
      mode is useful for modeling the effect of different types of memory
      systems on accelerators.

  3. **gem5-cpu**

    In this mode, you will write a program that runs on a gem5 simulated x86
    CPU.  The program can activate an accelerator modeled by Aladdin for
    specific kernels in the program by invoking special system calls.
    Communication between CPU and accelerator is achieved through shared
    memory. This is the most realistic way to model an accelerator system, but
    it also requires the most work to accomplish because it requires additional
    code to be written, whereas the other modes simply require additional
    configuration files that can be automatically generated.

  For this tutorial, we will run an Aladdin standalone design sweep.

### Define the design sweep ###

  The design sweep is defined by a Python file called `sweep_config.py`. We
  provide an example sweep file named `sweep_config_example.py`. Make a copy of
  this file, name it `sweep_config.py`, and place it in the same folder as the
  example.  This sweep file defines all parameters that can be swept, the range
  values, whether to sweep linearly or logarithmically, and other options. Each
  parameter is described by a SweepParam object, defined in
  `design_sweep_types.py`, where you can go look for more details.

  The example below shows how to define an unrolling factor sweep from 1 to 32 in
  exponential increments of 2 (1,2,4,8,16,32). The `short_name` parameter will be
  used to shorten the directory name for this design point (`unr_1` instead of
  `unrolling_1`).

  ```python
  unrolling = SweepParam(
      "unrolling", start=1, end=32, step=2, step_type=EXP_SWEEP, short_name="unr")
  ```

  Note that it is the first constructor parameter, a name, that identifies this
  parameter as the unrolling factor, rather than the name of the variable
  itself.

  The example sweep config file includes all the supported sweep parameters.
  Because they are the only parameters that are supported, you should not
  rename the parameters. In general, you should only need to change the start,
  end, step, and sweep type. The `short_name` parameter can be changed if you
  wish.

### Generate the configuration files ###

  To generate all required configuration files, run the following command:

  ```
  python generate_design_sweeps.py configs
      --output_dir machsuite
      --benchmark_suite machsuite
      --memory_type spad
      --simulator aladdin
  ```

  Parameters:
  1. `output_dir`: The directory for the entire design sweep. The directory
     structure is shown below.  Each leaf directory contains all the required
     configuration files for running an Aladdin standalone experiment.
  2. `benchmark_suite`: The benchmark suite to run.
  3. `memory_type`: Use scratchpads ("spad"), caches ("cache"), or both
    ("hybrid") in the simulation.
  4. `simulator`: The chosen simulation mode: "aladdin" (standalone),
    "gem5-cache", or "gem5-cpu".

```
machsuite
  |
  +-- aes-aes
  |   |
  |   +-- pipe_1_unr_1_part_1
  |   +-- pipe_1_unr_2_part_1
  |   +-- [continued for all design points]
  +-- bfs-bulk
  |   |
  |   +-- pipe_1_unr_1_part_1
  |   +-- pipe_1_unr_2_part_1
  |   +-- [continued...]
  +-- [continued for all benchmarks]
```

### Generate the dynamic trace ###

  We can also use the generator script to generate dynamic traces for each
  benchmark for MachSuite. In order to generate traces, you will need to install
  LLVM-Tracer as well as LLVM 3.4 (Aladdin's dependencies). You also need to set
  the environment variable `TRACER_HOME` to point to your LLVM-Tracer directory.

  Run the following command.

  ```
  python generate_design_sweeps.py trace
      --output_dir machsuite
      --benchmark_suite machsuite
      --source_dir [absolute path to MachSuite top level dir]
      --memory_type spad
  ```

  This creates an `inputs` directory containing the dynamic trace under each
  benchmark directory (e.g. `machsuite/aes-aes/inputs`).

### Run the design sweep ###

  We are now ready to run the design sweep, but we'll first do a dry run to
  illustrate what is happening.

  ```
  python generate_design_sweeps.py run
      --dry
      --output_dir machsuite
      --benchmark_suite machsuite
  ```

  This command generates a `run.sh` Bash script for each design point.
  Executing this script in a shell will run Aladdin on that design point. If we
  rerun the above command but omit the `--dry` parameter, each of these run
  scripts will be executed.

  Go ahead and run a simulation (pick a design point for a benchmark). When the
  simulation completes, you will see an `outputs` directory. This directory
  contains the final Aladdin summary file that reports area, power, and
  performance, as well as intermediate files generated by Aladdin and `stdout`
  and `stderr` dumps.

  If your computing environment uses the Condor job scheduling system, this
  script can generate a Condor job submission file as well.

  ```
  python generate_design_sweeps.py condor
      --output_dir machsuite
      --benchmark_suite machsuite
  ```

  The file is named `submit.con`, located under the `machsuite` folder.
