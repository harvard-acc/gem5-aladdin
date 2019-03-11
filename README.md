gem5-Aladdin SoC Simulator
==============================

[![build status](https://travis-ci.org/harvard-acc/gem5-aladdin.svg?branch=master)](https://travis-ci.org/harvard-acc/gem5-aladdin)

Welcome to the gem5-Aladdin SoC simulator!

This is a tool for end-to-end simulation of SoC workloads, including workloads
with accelerated functions handled by fixed-function hardware blocks. With
gem5-Aladdin, users can study the complex behaviors and interactions between
general-purpose CPUs and hardware accelerators, including but not limited to
cache coherency and memory consistency in heterogeneous platforms, data
movement and communication, and shared resource contention, and how all these
system-level effects impact overall application performance and speedup.

If you use gem5-Aladdin in your research, we would appreciate a citation to:

Co-Designing Accelerators and SoC Interfaces using gem5-Aladdin.
Yakun Sophia Shao, Sam (Likun) Xi, Vijayalakashmi Srinvisan, Gu-Yeon Wei, and David Brooks.
International Symposium on Microarchitecture (MICRO), June 2016.
[PDF](http://www.eecs.harvard.edu/~shao/papers/shao2016-micro.pdf)

If you have any questions, please send them to the gem5-aladdin users mailing
list (see link at the very bottom).

## Notices ##

#### December 1st, 2017 ####

All commits from gem5 upstream as of 01/24/19 have been merged into
gem5-Aladdin.  Notable changes:

* SystemC support in gem5.
* GTest framework for unit testing.

#### December 1st, 2017 ####

gem5-Aladdin now has a Docker image! This image has all of gem5-aladdin's
dependencies installed, and it comes with a basic set of development tools
(e.g. vim). If you are having issues building the simulator because of
dependency problems, please consider using Docker! The prebuilt image is
located on [Docker Hub](https://hub.docker.com/r/xyzsam/gem5-aladdin/).

See the `docker` directory for more details.

#### August 28th, 2017 ####

All commits from gem5 upstream as of 8/17/17 have been merged into
gem5-aladdin. Notable changes:

* SWIG has been replaced by PyBind11, so SWIG is no longer a dependency.
  PyBind11 comes packaged with gem5.
* There is a new SQL stats dump implementation. The previous version was
  written in Python, but is no longer compatible with PyBind11. To use the new
  implementation, you must install the SQLite3 development headers and
  libraries (in Ubuntu: `sudo apt install libsqlite3-dev`).

#### June 3rd, 2017 ####

This branch has been renamed from `devel` to `master` and is now the default
branch of this repository.

#### March 7th, 2017 ####

This branch of gem5-Aladdin is based on gem5's *development* branch.  The
original release of gem5-Aladdin (still accessible via this repository's
`stable-old` branch) was based on gem5's stable branch, which has
been deprecated. The development branch and the stable branch have *entirely
separate histories*. If you are a current user and you want to stay up to date
with gem5-Aladdin, you *must* check out a completely fresh branch. You cannot
simply merge the old branch with this new one!

We recommend that you clone a new local repository from this branch, rather
than trying to bring this into your current local repository. To do so:

    git clone -b devel https://github.com/harvard-acc/gem5-aladdin

The devel branch will soon be made the default branch, at which point you can
drop the `-b devel` argument.

## Requirements: ##

To build gem5-Aladdin, you will need to satisfy the dependencies of three
projects: gem5, Aladdin, and Xenon.

### gem5 dependencies ####

The main website can be found at http://www.gem5.org

A good starting point is http://www.gem5.org/Introduction, and for
more information about building the simulator and getting started
please see http://www.gem5.org/Documentation and
http://www.gem5.org/Tutorials.

To build gem5, you will need the following software: g++ or clang,
Python (gem5 links in the Python interpreter), SCons, ~~SWIG~~, zlib, m4,
and lastly protobuf if you want trace capture and playback
support. Please see http://www.gem5.org/Dependencies for more details
concerning the minimum versions of the aforementioned tools.

If you want gem5 to dump stats in SQLite databases for easy access, you
will also need to install SQLite3 development headers and libraries.

### Aladdin dependencies ####

The main Aladdin repository is [here](https://github.com/ysshao/aladdin).
Users are recommended to see Aladdin's README for detailed instructions on
installing dependencies.

In short, Aladdin's dependencies are:

1. Boost Graph Library 1.55.0+
2. GCC 4.8.1 or newer (we use C++11 features).
3. LLVM 3.4 and Clang 3.4, 64-bit
4. LLVM-Tracer ([link](https://github.com/ysshao/LLVM-Tracer.git)).

### Xenon dependencies ####

Xenon, the system we use for generating design sweep configurations, can be
found [here](https://github.com/xyzsam/xenon).

Xenon requires:

1. Python 2.7.6+
2. The pyparsing module

## Installation ##

### Setting up the source code ###

1. Clone gem5-Aladdin.

  ```
  git clone https://github.com/harvard-acc/gem5-aladdin
  ```

2. Setup the Aladdin and Xenon submodules.

  ```
  git submodule update --init --recursive
  ```

### Building gem5-Aladdin ###

gem5 supports multiple architectures, but gem5-Aladdin currently only supports
x86. ARM support is planned for a future release.

Type the following command to build the simulator:

  ```
  scons build/X86/gem5.opt
  ```

This will build an optimized version of the gem5 binary (gem5.opt) for the
specified architecture.  You do *not* need to build Aladdin separately, unless
you want to run Aladdin on its own. You can also replace `gem5.opt` with
`gem5.debug` to build a binary suitable for use with a debugger.  See
http://www.gem5.org/Build_System for more details and options.

The basic source release includes these subdirectories:
   - configs: example simulation configuration scripts
   - ext: less-common external packages needed to build gem5
   - src: source code of the gem5 simulator
   - system: source for some optional system software for simulated systems
   - tests: regression tests
   - util: useful utility programs and files

## Running gem5-Aladdin ##

gem5-Aladdin can be run in two ways: standalone and CPU.

In the standalone mode, there is no CPU in the system. gem5-Aladdin will simply
invoke Aladdin, but now you get access to the complete gem5 memory system
(where Aladdin alone supports private scratchpad memory only).  In CPU mode,
gem5-Aladdin will execute a user-level binary, which may invoke an accelerator
after setting the necessary input data. gem5-Aladdin uses the `num-cpus` command-line
parameter to distinguish between these two modes.

We have multiple integration tests that users can use as a starting point for
running the simulator. They are located in
`gem5-aladdin/src/aladdin/integration-test`, with both `standalone` and
`with-cpu` options. To run any integration test, simply change into the
appropriate directory and execute the following command:

  ```
  sh run.sh
  ```

If successful, the output of the simulator will be placed under the `outputs`
subdirectory, while the `stdout` dump will be preserved in `stdout.gz`.

## Writing an accelerated program ##

For an example of how to write a program that invokes an Aladdin accelerator,
we recommend starting with the integration tests `test_load_store` (which uses
caches only) and `test_dma_load_store` (which uses DMA only). Both of these
tests prepare data on the CPU, transfer the data into the accelerator, and
expect the accelerator to modify the data in a particular way and write it into
the memory system.

gem5-Aladdin does not currently support full-system simulation.

-----------------------

### Questions ###

We would appreciate if you post any questions to the gem5-aladdin users mailing
list.

[gem5-aladdin users mailing list](https://groups.google.com/forum/#!forum/gem5-aladdin-users)
