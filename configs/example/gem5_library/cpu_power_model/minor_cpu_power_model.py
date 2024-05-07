from m5.objects import PowerModel, PowerModelFunc
from m5.objects import BaseMinorCPU, BaseO3CPU

from .minor_alu_power_model import MinorALUPower
from .minor_rf_power_model import MinorRFPower
from .bimodal_bp_power_model import BimodalBPPower

"""
Is there a way include "Base_PowerModel as a class to derive from?
Main reason is to limit the amount of fns we override/redefine
"""


class CPUPowerOn(PowerModelFunc):
    """Power model for an Minor CPU."""

    def __init__(self, core, branch_pred):
        super().__init__()
        self._alu = MinorALUPower(core)
        self._rf = MinorRFPower(core, issue_width=4)
        self._bp = BimodalBPPower(core)

        self.dyn = self.dynamic_power
        self.st = self.static_power

    def static_power(self):
        """Returns static power in Watts"""
        return 1.0

    def dynamic_power(self):
        """Returns dynamic power in Watts"""
        total = 0.0
        for part in [self._alu, self._rf, self._bp]:
            # Note: There may be side effects of calling this function
            # (e.g., remembering the last value of the stat). So, I would
            # only call it once
            power = part.dynamic_power()
            print(f"{part.name} dynamic power is: {power}")
            total += power

        return total


class CPUPowerOff(PowerModelFunc):
    def __init__(self):
        super().__init__()
        self.dyn = lambda: 0.0
        self.st = lambda: 0.0


class MinorPowerModel(PowerModel):
    def __init__(self, core, branch_pred):
        super().__init__()
        # Choose a power model for every power state
        self.pm = [
            CPUPowerOn(core, branch_pred),  # ON
            CPUPowerOff(),  # CLK_GATED
            CPUPowerOff(),  # SRAM_RETENTION
            CPUPowerOff(),  # OFF
        ]
