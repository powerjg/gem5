Authors: Jason Lowe-Power
         Sean Wilson

This file explains how to use gem5's updated testing infrastructure. Running tests before submitting a patch is *incredibly important* so unexpected bugs don't creep into gem5.

gem5's testing infrastructure has the following goals:
 * Simple for *all* users to run
 * Fast execution in the simple case
 * High coverage of gem5 code

# Running tests

```shell
cd tests
./main.py run --tags=quick
```

# If something goes wrong

## Debugging the testing infrastructure

Every command takes an option for the verbosity. `-v`, `-vv`, `-vvv` will increase the verbosity level. If something isn't working correctly, you can start here.
