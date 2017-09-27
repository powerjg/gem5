Authors: Jason Lowe-Power
         Sean Wilson

This file explains how to use gem5's updated testing infrastructure. Running
tests before submitting a patch is *incredibly important* so unexpected bugs
don't creep into gem5.

gem5's testing infrastructure has the following goals:
 * Simple for *all* users to run
 * Fast execution in the simple case
 * High coverage of gem5 code

# Running tests

Below is the most common way the tests are run. This will run all of the "quick"
tests with ``gem5.opt`` for the x86 ISA. Of course, if your changes touch other
ISAs you should test all ISAs or the ISA that concerns your change.
Additionally, it is often a good idea to run longer tests (e.g., linux boot)
before submitting your patch.

```shell
cd tests
./main.py run --length quick --optimization opt --isa x86
```

## Specifying a subset of tests to run

You can use the tag query interface to specify the exact tests you want to run.
For instance, if you want to run only with `gem5.opt`, you can use

```shell
./main.py run --optimization opt
```

To view all of the available tags, use

```shell
./main.py list --all-tags
```

The output is split into tag *types* (e.g., isa, optimization, length) and the
tags for each type are listed after the type name.

You can specify "or" between tags within the same type by using the tag flag
multiple times. For instance, to run everything that is tagged "opt" or "fast"
use

```shell
./main.py run --optimization opt --optimization fast
```

You can also specify "and" between different types of tags by specifying more than one type on the command line. For instance, this will only run tests with both the "X86" and "opt" tags.

```shell
./main.py run --isa X86 --optimization opt
```

# Binary test applications

The code for test binaries can be found in ``tests/test-progs``.
Here, there's one directory per test application.
The source code is under the ``source`` directory.

You may have a ``bin`` directory as well.
The ``bin`` directory is automatically created when running the test case that uses the test binary.
The binary is downloaded from the gem5 servers the first time it is referenced by a test.

## Updating the test binaries

The test infrastructure should check with the gem5 servers to ensure you have the latest binaries.
However, if you believe your binaries are out of date, simply delete the ``bin`` directory and they will be re-downloaded to your local machine.

## Building (new-style) test binaries

In each ``src/`` directory under ``tests/test-progs``, there is a Makefile.
This Makefile downloads a docker image and builds the test binary for some ISA (e.g., Makefile.x86 builds the binary for x86).
Additionally, if you run ``make upload`` it will upload the binaries to the gem5 server, if you have access to modify the binaries.
*If you need to modify the binaries for updating a test or adding a new test and you don't have access to the gem5 server, contact a maintainer (see MAINTAINERS).*

# If something goes wrong

The first step is to turn up the verbosity of the output using `-v`. This will
allow you to see what tests are running and why a test is failing.

If a test fails, the temporary directory where the gem5 output was saved is kept and the path to the directory is printed in the terminal.

## Debugging the testing infrastructure

Every command takes an option for the verbosity. `-v`, `-vv`, `-vvv` will increase the verbosity level. If something isn't working correctly, you can start here.
