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
# Authors: Brian Coutinho, Keshav Mathur, Jason Lowe-Power

""" Statistical Sampling library for gem5:

    This library provides support for performing statistical sampling
    on multi-threaded workloads.  See the accompanying example script
    files for examples on using this library.

    ----------------------
    What this module expects:
    The system object must have a three cpu models named as follows-

      system.cpu : should be a list of type KVM cpu to enable KVM
                   fast-forward mode
      system.atomicCpu : should be a matching sized list of the
                   AtomicSimpleCPU type for warming up caches etc.
      system.timingCPU: should contain instances of the cpu type
                   you wish to use in the experiments

    Secondly, if you simulate a multi-threaded application the system
    should have some componenet that adds a small amount of non-determinism
    to each simulation. In the example scripts we add a small random delay
    on every last level cache miss to create this effect. The module
    changes the random seed for each simulation at a sample point in order
    to cover all divergent paths.
    ----------------------

    Please look at the technical report below for details on how to
    determine sampling paramters and a recommended flowchart for the
    sampling process
    <TBD>
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


SimpleOpts.add_option("--warmup_time", default="10ms",
                      help="Simulated functional warmup time in ms")
SimpleOpts.add_option("--detailed_warmup_time", default="10us",
                      help="Simulated detailed warmup time in ms")
SimpleOpts.add_option("--detailed_time", default="1ms",
                      help="Simulated time in ms for detailed measurements, \
                          (size of measurment window)")

from sim_tools import fastforward, runSim, warmupAndRun

_valid_system = False

def checkSystem(system):
    """ Do some basic validation of the system such as does it
        have the required objects, uses a KVM cpu etc.
        This also adds some helper functions used by the Sampling
        module.
    """
    for cpulist in ["cpu", "atomicCpu", "timingCpu"]:
        if not (hasattr(system, cpulist) and \
                 isinstance( getattr(system, cpulist), list) ):
            print >> sys.stderr, "System should have three lists of"\
                                 " cpus named- cpu, atomicCpu, timingCpu"
            sys,exit(1)

    if not isinstance(system.cpu[0], BaseKvmCPU):
      print >> sys.stderr, "system.cpu should be of KVM cpu time !"
        sys,exit(1)
    if not isinstance(system.atomicCpu[0], AtomicSimpleCPU):
      print >> sys.stderr, "system.atomicCpu should be of type AtomicSimpleCPU"
        sys,exit(1)

    # system object helper functions
    def switchCpus(self, old, new):
        assert(new[0].switchedOut())
        m5.switchCpus(self, zip(old, new))

    def totalInsts(self):
        return sum([cpu.totalInsts() for cpu in self.cpu])

    if not hasattr(system, 'switchCpus'):
        system.switchCpus = classmethod(switchCpus)
    if not hasattr(system, 'totalInsts'):
        system.totalInsts = classmethod(totalInsts)

    _valid_system = True


def sampleROI(system, opts, instructions, samples, runs):
    """ At a high-level, this function executes the entire ROI and takes
        a number of random sample points. At each point it may run multiple
        simulations.
        This uses gem5 fork support for creating multiple simulations that
        could diverge

        @param system the system we are running
        @param opts command line options as an instance of optparse
        @param instructions total instructions in the ROI (summed over all
               CPUs)
        @param samples number of sample points in the ROI
        @param runs per sample point
    """
    from random import randint, sample, seed
    import signal
    import time

    if not _valid_system:
        checkSystem(system)

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
