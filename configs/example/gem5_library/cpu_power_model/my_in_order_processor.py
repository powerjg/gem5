# Copyright (c) 2024 The Regents of the University of California
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

""""
This file contains an example out-of-order processor with a power model.
"""

from gem5.isas import ISA
from gem5.components.processors.base_cpu_processor import BaseCPUProcessor
from gem5.components.processors.base_cpu_core import BaseCPUCore

from m5.objects import X86MinorCPU, BiModeBP

from .minor_cpu_power_model import MinorPowerModel


class MyInOrderCore(BaseCPUCore):
    """
    This is a simple OOO core without any parameters.
    It uses the defaults from gem5's O3 CPU.
    """

    def __init__(self):
        super().__init__(
            core=X86MinorCPU(),
            isa=ISA.X86,
        )
        # Here, you could customize parameters of the O3CPU
        # Alternatively, (and to follow better design practices) you could
        # create a new class that inherits from X86MinorCPU and customize it

        # Set the branch predictor to be a BiModeBP
        # Note that the parameter "branchPred" is defined in the specialization
        # of O3, Minor, and TimingSimpleCPU not in the BaseCPU (probably should
        # be fixed...)
        self.core.branchPred = BiModeBP()

        self.power_model = MinorPowerModel(self, self.core.branchPred)


class MyInOrderProcessor(BaseCPUProcessor):
    """
    This is a simple OOO processor with a single core.
    """

    def __init__(self):
        super().__init__(cores=[MyInOrderCore()])
