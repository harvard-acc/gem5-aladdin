Design Space Exploration with gem5-Aladdin
==========================================

gem5-Aladdin is designed to enable fast design sweeps of accelerator workloads,
but manually writing configuration files for each design point quickly gets
tedious. We use the Xenon system to generate design sweep configurations.
This README will show you how to get started. For more information about
Xenon, please see [Xenon](https://github.com/xyzsam/xenon).


Prerequisites
--------------

You need to have the pyparsing module installed. Versions supported are between
2.2.0-2.3.0 (inclusive).

  ```
  pip install pyparsing==2.3.0
  ```

Getting started
---------------

Try out using the prepared configurations for the MachSuite and SHOC benchmark
suites.

  ```
  python generate_design_sweeps.py benchmarks/machsuite.xe
  python generate_design_sweeps.py benchmarks/shoc.xe
  ```

The results of the design sweeps will be placed under the `machsuite` and
`shoc` subdirectories, respectively. The directory structure for MachSuite
might look like so:

  ```
  machsuite
    |
    + aes-aes
      |
      + inputs
        |
        + dynamic_trace.gz
      + 0
        |
        + aes-aes.cfg
        + gem5.cfg
        + run.sh
        + ... and other configuration files ...
      + 1
      + 2
      + ... and all other configurations ...
    + bfs-bulk
    + ... and all other benchmarks ...
  ```

To run one of these benchmarks, simply go into one of the configuration folders
and execute the following command:

  ```
  bash run.sh
  ```

If successful, simulation output will be placed into the `outputs` subdirectory.

Quick overview
--------------

The file that describes the MachSuite benchmark suite is
`benchmarks/machsuite.py`. This file defines each benchmark as an object of the
Python module. Each benchmark contains information about its functions, loops,
and arrays. Datatypes are defined in `benchmarks/datatypes.py`.

The file that defines the parameters of the design sweep is
`benchmarks/machsuite.xe`.  This sweep file defines the design sweep parameters
and generation targets.

The constant design sweep parameters that a sweep script can set are:

  * `output_dir`: The directory where design sweeps will be placed.
  * `source_dir`: The location of the source code for the benchmark.
  * `simulator`: The simulator to use. Options are: aladdin, gem5-cache, and
    gem5-cpu. See `benchmarks/designsweeptypes.py` for a description of what each
    simulator value means.
  * `memory_type`: The type of memory system to use.

In addition, any sweep parameter defined in `benchmarks/params.py' can be swept
or set. `machsuite.xe`, by default, sweeps the system clock time from 1 to 5ns.
Please read the Xenon documentation to learn how to use the Xenon language - it
is very simple and straightforward.

The targets that sweep scripts can generate are:

  * `configs`: All design sweep configurations and required files.
  * `trace`: The dynamic traces required for each benchmark. Since all
    configurations of a design sweep for a benchmark use the same trace, and
    traces can take much longer to generate than design sweep configurations,
    this component is specified separate.
  * `dma_trace`: Same as above, but DMA load and store function calls are
    included.

Be careful that you choose the right trace generation target! Use the DMA
version when you expect data for arrays to be supplied through DMA, and use the
non-DMA version if you are using caches or running Aladdin in standalone. Picking
the wrong one can lead to simulation deadlocks or assertion failures.

This script also sources a set of constant values for MachSuite benchmarks.
These constant values are a set of recommended values that will generally produce
sensible results. For example, small arrays are partitioned completely into
registers, and innermost loops are flattened, leaving partitioning factors to
be set on outer loops. Any of these can be changed as desired.

Using your own benchmark suites
-------------------------------

The recommended approach is to simply copy one of the existing benchmark suite
definition files (like `benchmarks/machsuite.py`) and modify it for your
applications.

One limitation of the current system is with regards to multiple datasets.
Aladdin needs to know the size of each array in the benchmark to know how it
should be partitioned, and currently these sizes are specified through the
benchmark definition file. In order to support multiple datasets with different
sizes, currently you must write a separate file for data size. We are working
on a solution to embed these information in the dynamic trace file, similar to how
we handle loop label names.

Help
----

Please don't hesitate us if you have any questions.

**NOTE**: If you are not yet familiar with Aladdin's execution model and
simulation methodology, or with gem5-Aladdin, you are advised to read the two
following papers and familiarize yourself with running Aladdin
simulations before continuing.

Co-Designing Accelerators and SoC Interfaces using gem5-Aladdin.
Yakun Sophia Shao, Sam Likun Xi, Viji Srinivasan, Gu-Yeon Wei, and David Brooks.
International Symposium on Microarchitecture, October 2016

Aladdin: A Pre-RTL, Power-Performance Accelerator Simulator Enabling Large
Design Space Exploration of Customized Architectures,
Yakun Sophia Shao, Brandon Reagen, Gu-Yeon Wei and David Brooks,
International Symposium on Computer Architecture, June, 2014

Sam Xi: samxi@seas.harvard.edu

Sophia Shao: sshao@nvidia.com
