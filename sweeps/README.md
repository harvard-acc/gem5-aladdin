Design Space Exploration with Aladdin and gem5
==============================================

Aladdin is designed to enable fast design sweeps of accelerator workloads, but
manually writing configuration files for each design point quickly gets
tedious. We have designed a flexible system for automatically generating design
sweeps. This README will show you how to generate these sweeps.

The central piece of this system is the `generate_design_sweeps.py` script.
This script can generate all required configuration files, Bash scripts, and if
configured correctly, benchmark dynamic traces as well.

**Table of Contents**
  1. [Tutorial](docs/tutorial.md) - Walks through the process of running the
     generator script for a prepared benchmark suite.
  2. [Writing a benchmark configuration file](docs/benchmarkconfig.md) -
     Describes how to write a benchmark configuration file in detail.
  3. [Running gem5 and Aladdin simulations with a CPU](docs/gem5-cpu.md) -
     Describes the gem5-aladdin integration and how to invoke an accelerator
     from a user program running on a simulated CPU in gem5.

**NOTE**: If you are not yet familiar with Aladdin's execution model and
simulation methodology, you are advised to read the ISCA paper and familiarize
yourself with running Aladdin simulations before continuing.


Aladdin: A Pre-RTL, Power-Performance Accelerator Simulator Enabling Large
Design Space Exploration of Customized Architectures,
Yakun Sophia Shao, Brandon Reagen, Gu-Yeon Wei and David Brooks,
International Symposium on Computer Architecture, June, 2014

Thanks,

Sophia Shao and Sam Xi

Harvard University
