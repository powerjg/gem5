from m5.objects import PowerModel, PowerModelFunc
from m5.stats import *

from .alu_power_model import *
from .rf_power_model import *
from .bimodal_bp_power_model import *
from .base_power_model import *

"""
Is there a way include "Base_PowerModel as a class to derive from?
Main reason is to limit the amount of fns we override/redefine
"""


class CPU_PowerOn(PowerModelFunc):
    def __init__(self, core):
        super().__init__()
        self._core = core

        self._alu = ALU_PowerOn(core)
        self._rf = RF_PowerOn(core)
        self._bp = BimodalBP_PowerOn(core)

        self.dyn = lambda: self.dynamic_power()
        self.st = lambda: self.static_power()

    def static_power(self):
        """Returns static power in Watts"""
        return 1.0

    def dynamic_power(self):
        """Returns dynamic power in Watts"""
        total = 0.0
        for part in [self._alu, self._rf, self._bp]:
            total += part.dynamic_power()

        return total


class CPU_PowerOff(PowerModelFunc):
    def __init__(self):
        super(CPU_PowerOff, self).__init__()
        self.dyn = lambda: 0.0
        self.st = lambda: 0.0


class CPU_PowerModel(PowerModel):
    def __init__(self, core, **kwargs):
        super(CPU_PowerModel, self).__init__(**kwargs)
        # Choose a power model for every power state
        self.pm = [
            CPU_PowerOn(core),  # ON
            CPU_PowerOff(),  # CLK_GATED
            CPU_PowerOff(),  # SRAM_RETENTION
            CPU_PowerOff(),  # OFF
        ]
