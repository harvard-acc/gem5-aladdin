Benchmark Configuration File
----------------------------

The benchmark configuration file defines all the functions (kernels), arrays,
and loops in a benchmark. It also specifies the location of the source code.

We have provided two example benchmark config files for SHOC and MachSuite,
which are `shoc_config.py` and `machsuite_config.py`, respectively. The process
is mostly self-explanatory, but below, we show to write a configuration for the
AES benchmark in MachSuite.

**Overall steps**

  1. Create the benchmark object.
  2. Specify the kernels.
  3. Define all arrays.
  4. Define all loops.

### Create the benchmark object ###

  Create a benchmark object for each benchmark in a suite. You will need the
  complete path to each benchmark, divided like so:

  `[local path to top level benchmark] / [relative path to each benchmark]`

  So, if the path to aes.c is `/home/user/aladdin/MachSuite/aes/aes`, then the
  path to the top level benchmark is `/home/user/aladdin/MachSuite`, and the
  relative path to the AES benchmark is `aes/aes`. The AES benchmark object would
  be constructed like so:

  ```
  aes = Benchmark("aes", "aes/aes")
  ```

  The path to the top level benchmark is provided through the `source_dir` parameter
  when the user runs the generator script.

### Specify the kernels ###

  Specify the set of functions that Aladdin will be analyzing as a list of
  strings. For AES, there are 12 kernels. We would set this list as follows:

  ```
  aes.set_kernels(["gf_alog", "gf_log", "gf_mulinv", "rj_sbox", "rj_xtime",
                  "aes_subBytes", "aes_addRoundKey", "aes_addRoundKey_cpy",
                  "aes_shiftRows", "aes_mixColumns", "aes_expandEncKey",
                  "aes256_encrypt_ecb"])
  ```

  The LLVM-Tracer tool will use this list to construct the `WORKLOAD` environment
  variable, which is just a comma separated list of the kernel names.

### Define all arrays ###

  For each array in each kernel listed above, provide the name of the array, its
  size in *elements*, the size of each element in *bytes*, and the partitioning
  type (cyclic, block, or complete). Remember that the size of an array must be
  STATIC; while the array itself can be dynamically allocated at initialization,
    its size must not change during execution.

  ```
  benchmark.add_array(name, num_elements, elem_sz_bytes, partition_method, memory_type=SPAD)
  ```

  Valid values for `partition_method` are:
  `PARTITION_CYCLIC, PARTITION_BLOCK, PARTITION_COMPLETE`.

  The final (optional) parameter, `memory_type`, specifies whether the array
  should be stored in a software-managed scratchpad or a hardware-managed cache.
  By default, all arrays are stored in scratchpads.  To use a cache, pass the
  constant `CACHE` as the final argument.  All of these constants are defined in
  `design_sweep_types.py`.

  Example with AES:

  ```
  aes.add_array("ctx", 96, 1, PARTITION_CYCLIC)
  ```

### Define all loops ###

  For each loop in each kernel listed above, provided the name of the parent
  function, the line number where the loop begins, and the trip count of the
  loop.

  ```
  benchmark.add_loop(parent_function, line_number, trip_count)
  ```

  **Arguments**:
  * `parent_function`: The name of the function the loop is inside.
  * `line_number`: The location where a branch direction is computed. In
    other words, it is the line containing a `for` or `while` keyword. For
    do-while loops, this means that the line is the *last* line of the loop.
    Avoid breaking up these lines onto multiple lines; while we are advocates of
    good coding style, doing so can confuse LLVM-Tracer.  Remember that these
    line numbers must be updated if you insert any code into the file that
    changes the locations of the loops. This is the most fragile aspect of the
    design sweep system, and we are working on using labels inside of line
    numbers as loop identifiers.
  * `trip_count`: How many times the loop iterates. This is used to
    determine how the loop shall be unrolled - for example, if a loop only runs 9
    times but the design sweep specifies unrolling factor of 32, Aladdin will not
    unroll this loop, and we have to tell Aladdin to flatten it instead. As this
    is tedious to determine and specify per loop, we also provide three special
    values:

      - `UNROLL_ONE`: Do not unroll this loop. Useful for outermost loops.
      - `UNROLL_FLATTEN`: Flatten this loop completely. Useful for
          innermost loops.
      - `ALWAYS_UNROLL`: Unroll just based on the unrolling factor.  As
        long as the unrolling factor is smaller than the trip count observed
        in the trace, Aladdin will automatically account for any remainder
        iterations (e.g. a 9-iteration loop with unrolling factor 4 has 1
        remainder iteration).

### gem5-cpu experiments ###

  There are some additional steps that are needed for gem5-cpu experiments. Refer
  to the documentation [here](gem5-cpu.md) for more information.
