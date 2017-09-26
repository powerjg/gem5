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

```shell
cd tests
./main.py run --length quick
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

You can also specify "and" between different types of tags by sepcifying more than one type on the command line. For instance, this will only run tests with both the "X86" and "opt" tags.

```shell
./main.py run --isa X86 --optimization opt
```

# If something goes wrong

The first step is to turn up the verbosity of the output using `-v`. This will
allow you to see what tests are running and why a test is failing.

If a test fails, the temporary directory where the gem5 output was saved is kept and the path to the directory is printed in the terminal.

## Debugging the testing infrastructure

Every command takes an option for the verbosity. `-v`, `-vv`, `-vvv` will increase the verbosity level. If something isn't working correctly, you can start here.
