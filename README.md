gem5-Aladdin SoC Simulator
==============================

[![harvard-acc](https://circleci.com/gh/harvard-acc/gem5-aladdin.svg?style=shield)](https://circleci.com/gh/harvard-acc/gem5-aladdin)

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

#### Feburary 1st, 2020 ####

Major update for gem5-Aladdin to version 2.0:

* Aladdin v2.0 brings support for tracing C++ programs. Requires an update to
  LLVM 6.0.
* Sampling support for accelerated kernels.
* New systolic array cycle-level model.
* Interrupt-like mechanism for waking up accelerators (instead of spin locks
  and polling).
* Accelerator command queuing. Multiple invocations of an accelerator can be
  pushed to a queue, and the system will run each of them until the queue is
  empty, without the need for the main CPU thread to intervene.
* Accelerator coherency port (ACP) for one-way cache coherency.
* Change the configured memory type of an array on the fly, so a program can
  dynamically decide whether to transfer data over DMA, ACP, or hardware cache
  coherency.
* The "standalone" simulation mode (accelerator and memory system but no CPU)
  has been deprecated.
* Merge with gem5 upstream. Includes all commits as of 377898c.

A new docker image has been pushed with environment updates for all of these
changes. It is tagged `llvm-6.0`. Download
[here](https://hub.docker.com/repository/docker/xyzsam/gem5-aladdin).

Usage of a few new features:

* Tracing C++ programs

As an example shown below, now we can trace a C++ program, as long as the traced
function is written in pure C with C-style linkage (the top\_level function in
the example is in an `extern C` context to ensure C-style linkage).

```c
// Though we can write C++ code, only code with external C-style linkage will be
// instrumented (extern "C").
#ifdef __cplusplus
extern "C" {
#endif
int top_level(int a, int b) { return a + b; }
#ifdef __cplusplus
}
#endif

class Adder {
 public:
  Adder(int _a, int _b) : a(_a), b(_b) {}
  int run() {
    // The traced function needs to be pure C.
    return top_level(a, b);
  }

 private:
  int a;
  int b;
};

int main() {
  Adder adder(2, 3);
  int result = adder.run();
  std::cout << "result: " << result << "\n";
  return 0;
}
```

* Sampling support for accelerated kernel

Some workloads are highly compute and memory intensive, such that simulating the
complete workload would be infeasible because of trace storage limitations and
simulation time. To make it possible to simulate bigger workloads, we introduce
sampling support, which works at the loop granularity. The example below shows
how to use the sampling API. The user informs Aladdin the sampled loop label and
the sampling factor via a call to setSamplingFactor. Aladdin will unsample the
sampled loop iterations and produce a final overall cycles estimate.

```c
int reduction(int* a, int size, int sample) {
    // The DMA load is an operation that happens outside of the loop so it
    // doesn't need to be sampled.
    dmaLoad(a, size * sizeof(int));
    int result = 0;
    setSamplingFactor("loop", (float)size / sample);
    loop:
    // Run only `sample` iterations of this loop; the result
    // might be wrong, but that's expected for sampling.
    for (int i = 0; i < sample; i++)
        result += a[i]; return result;
}
```

* New systolic array cycle-level model

We added a cycle-level model for a 2D systolic array accelerator, which supports
computing convolution and matrix multiplication. Check out the usage in
`src/systolic_array/test`.

* Interrupt-like mechanism for synchronization between CPUs and accelerators

This eliminates the polling involved in the CPUs while waiting for the
accelerators to finish the assigned work. Instead, it puts the CPU to sleep
using a magic simulator instruction. The accelerator will wake the CPU when it
is done. This should also speed up simulation time by eliminating all the
redundant memory operations required by polling. Please see
`src/aladdin/integration-test/with-cpu/test_multiple_accelerators` as an
example.

* Support for Accelerator coherency port (ACP)

This is another choice for accelerator SoC interface, other than using
software-managed DMAs and fully-coherent caches. It enables the accelerator to
directly access the coherent data in the last-level-cache (LLC) without having a
private cache. For more details and usage, please see the `test_acp` of the
integration tests.

We also have a way to dynamically change the configured memory type of an array,
using the setMemoryType API (see the integration test `test_host_load_store`).

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
2. The pyparsing module (any version between 2.2.0 and 2.3.0, inclusive).

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
