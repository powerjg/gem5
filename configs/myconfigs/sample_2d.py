# -*- coding: utf-8 -*-
# Copyright (c) 2016 Jason Lowe-Power
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Jason Lowe-Power, Brian Coutinho

""" This script is an example of multi-threaded statistical sampling
    where we simulate a sample multiple times to cover several possible
    thread interleavings. Random pertubations are added during the
    simulation to exercise mutliple execution paths/interleavings.

    The sampling process thus works as two nested for-loops,
    the outer one for each sample point in the program and
    the inner one for mulitple observations at each sample point.

    This script takes three arguments, the total number of instructions in the
    ROI (which you can find by using runkvm.py), the number of sample
    points you want to take during the execution, and the number of runs
    to make at each sample point.

    The output is a set of two levels of directories in the outdir.
    At the top level is directory for each sample point which in turn
    contains multiple sub-directories for each random observation.

"""

import os
import sys
import time

import m5
from m5.objects import *
from m5.ticks import fromSeconds
from m5.util.convert import toLatency

sys.path.append('configs/common/') # For the next line...
import SimpleOpts

from system import MySystem

sys.path.append('configs/myconfigs/') # For the next line...
from sample import fastforward, runSim, warmupAndRun

SimpleOpts.set_usage("usage: %prog [options] roi_instructions \
samples=1 runs=1")


def simulateROI(system, instructions, samples, runs):
    """ Do everything we need to do during the ROI.
        At a high-level, this function executes the entire ROI and takes
        a number of random sample points. At each point it may run multiple
        simulations.

        @param system the system we are running
        @param instructions total instructions in the ROI (summed over all
               CPUs)
        @param samples number of sample points in the ROI
        @param runs per sample point
    """
    from random import randint, sample, seed
    import signal
    import time

    seed()

    executed_insts = system.totalInsts()

    warmup_time = opts.warmup_time
    detailed_warmup_time = opts.detailed_warmup_time
    detailed_time = opts.detailed_time

    # We probably want to updated the max instructions by subtracting the
    # insts that we are going to execute in the last detailed sim.
    sample_secs = toLatency(warmup_time) + toLatency(detailed_warmup_time) \
                    + toLatency(detailed_time)
    # assume 1 IPC per CPU
    instructions -= int(sample_secs * system.clk_domain.clock[0].frequency *
                        len(system.cpu) * 1)

    # Get the instruction numbers that we want to exit at for our sample
    # points. It needs to be sorted, too.
    sample_insts = sample(xrange(executed_insts,
                                 executed_insts + instructions),
                          samples)
    sample_insts.sort()

    # These are the currently running processes we have forked.
    pids = []

    # Signal handler for when the processes exit. This is how we know when
    # to remove the child from the list of pids.
    def handler(signum, frame):
        assert(signum == signal.SIGCHLD)
        try:
            while 1:
                pid,status = os.wait()
                if status != 0:
                    print "pid", pid, "failed!"
                    sys.exit(status)
                pids.remove(pid)
        except OSError:
            pass

    # install the signal handler
    signal.signal(signal.SIGCHLD, handler)

    # Here's the magic
    for i, insts in enumerate(sample_insts):
        print "Fast forwarding to sample", i, "stopping at", insts
        insts_past = fastforward(system, insts - system.totalInsts())
        if insts_past/insts > 0.01:
            print "WARNING: Went past the goal instructions by too much!"
            print "Goal: %d, Actual: %d" % (insts, system.totalInsts())

        # Max of 4 gem5 instances (-1 for this process). If we have this
        # number of gem5 processes running, we should wait until one finishes
        while len(pids) >= 4 - 1:
            time.sleep(1)

        # Now that we have hit the sample point take multiple observations
        parent = m5.options.outdir
        os.mkdir('%(parent)s/'%{'parent':parent} + str(insts))

        # Clone a new gem5 process for each observation
        for r in range(runs):
            # Fork gem5 and get a new PID. Save the stats in a folder based on
            # the instruction number and subdir based on run no.
            pid = m5.fork('%(parent)s/'+str(insts)+'/'+str(r))
            if pid == 0: # in child
                from m5.internal.core import seedRandom
                # Make sure each instance of gem5 starts with a different
                # random seed. Can't just use time, since this may occur
                # multiple times in the same second.
                rseed = int(time.time()) * os.getpid()
                seedRandom(rseed)
                print "Running detailed simulation for sample:", i, "run:", r
                warmupAndRun(system, warmup_time , detailed_warmup_time, \
                              detailed_time)
                print "Done with detailed simulation for sample:", i, "run:", r
                # Just exit in the child. No need to do anything else.
                sys.exit(0)
            else: # in parent
                # Append the child's PID and fast forward to the next point
                pids.append(pid)

    print "Waiting for children...", pids
    while pids:
        time.sleep(1)

if __name__ == "__m5_main__":
    (opts, args) = SimpleOpts.parse_args()

    # create the system we are going to simulate
    system = MySystem(opts)

    if not (len(args) == 1 or len(args) == 2 or len(args) == 3):
        SimpleOpts.print_help()
        fatal("Simulate script requires one or two or three arguments")

    roiInstructions = int(args[0])

    if len(args) == 2 or len(args) == 3:
        samples = int(args[1])
    else:
        samples = 1

    if len(args) == 3:
        runs = int(args[2])
    else:
        runs = 1

    # For workitems to work correctly
    # This will cause the simulator to exit simulation when the first work
    # item is reached and when the first work item is finished.
    system.work_begin_exit_count = 1
    system.work_end_exit_count = 1

    # Read in the script file passed in via an option.
    # This file gets read and executed by the simulated system after boot.
    # Note: The disk image needs to be configured to do this.
    system.readfile = opts.script

    # set up the root SimObject and start the simulation
    root = Root(full_system = True, system = system)

    if system.getHostParallel():
        # Required for running kvm on multiple host cores.
        # Uses gem5's parallel event queue feature
        # Note: The simulator is quite picky about this number!
        root.sim_quantum = int(1e9) # 1 ms

    # Disable the gdb ports. Required for high core counts and forking.
    m5.disableAllListeners()

    # instantiate all of the objects we've created above
    m5.instantiate()

    globalStart = time.time()

    # Keep running until we are done.
    print "Running the simulation"
    exit_event = m5.simulate()
    while exit_event.getCause() != "m5_exit instruction encountered":
        if exit_event.getCause() == "user interrupt received":
            print "User interrupt. Exiting"
            break
        print "Exited because", exit_event.getCause()

        if exit_event.getCause() == "work started count reach":
            simulateROI(system, roiInstructions, samples, runs)
        elif exit_event.getCause() == "work items exit count reached":
            end_tick = m5.curTick()

        print "Continuing"
        exit_event = m5.simulate()

    print
    print "Performance statistics"

    print "Ran a total of", m5.curTick()/1e12, "simulated seconds"
    print "Total wallclock time: %.2fs, %.2f min" % \
                (time.time()-globalStart, (time.time()-globalStart)/60)


