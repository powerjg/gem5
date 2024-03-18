from m5.objects import PowerModel, PowerModelFunc
from m5.stats import *
from .base_power_model import *


class ALU_PowerOn(Base_PowerModel):
    def __init__(self, simobj):
        super(ALU_PowerOn, self).__init__(simobj)
        self._alu = self.simobj
        self.dyn_fns = [self.int_energy, self.fp_energy, self.vec_energy]

    def static_power(self):
        """Returns static power in Watts"""
        return 1.0

    def int_energy(self):
        int_accesses = self.validate_stat("executeStats0.numIntAluAccesses")
        return int_accesses * self.int_alu_act_energy()

    def fp_energy(self):
        fp_accesses = self.validate_stat("executeStats0.numFpAluAccesses")
        return fp_accesses * self.fp_alu_act_energy()

    def vec_energy(self):
        vec_accesses = self.validate_stat("executeStats0.numFpAluAccesses")
        return vec_accesses * self.vec_alu_act_energy()

    def fp_alu_act_energy(self):
        return 3.5

    def int_alu_act_energy(self):
        return 1.0

    def vec_alu_act_energy(self):
        return 2.5


class ALU_PowerOff:
    def __init__(self):
        super(ALU_PowerOff, self).__init__()
        self.dyn = lambda: 0.0
        self.st = lambda: 0.0


class ALU_PowerModel:
    def __init__(self, core, **kwargs):
        super(ALU_PowerModel, self).__init__(**kwargs)
        # Choose a power model for every power state
        self.pm = [
            ALU_PowerOn(core),  # ON
            ALU_PowerOff(),  # CLK_GATED
            ALU_PowerOff(),  # SRAM_RETENTION
            ALU_PowerOff(),  # OFF
        ]
