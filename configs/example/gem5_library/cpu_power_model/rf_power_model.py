from m5.objects import PowerModel, PowerModelFunc
from m5.stats import *
from .base_power_model import *


class RF_PowerOn(Base_PowerModel):
    def __init__(self, simobj, issue_width=1):
        super(RF_PowerOn, self).__init__(simobj)
        self.dyn_fns = [self.int_energy, self.fp_energy, self.misc_energy]
        self._rf = self.simobj
        self.issue_width = issue_width

    def static_power(self):
        """Returns static power in Watts"""
        return 1.0

    def int_energy(self):
        reads = self.validate_stat("executeStats0.numIntRegReads")
        writes = self.validate_stat("executeStats0.numIntRegWrites")

        return (
            reads * self.rf_int_read_act_energy()
            + writes * self.rf_int_write_act_energy()
        )

    def fp_energy(self):
        reads = self.validate_stat("executeStats0.numFpRegReads")
        writes = self.validate_stat("executeStats0.numFpRegWrites")

        return (
            reads * self.rf_fp_read_act_energy()
            + writes * self.rf_fp_write_act_energy()
        )

    def misc_energy(self):
        reads = self.validate_stat("executeStats0.numMiscRegReads")
        writes = self.validate_stat("executeStats0.numMiscRegWrites")

        return (
            reads * self.rf_misc_read_act_energy()
            + writes * self.rf_misc_write_act_energy()
        )

    def rf_port_energy(self):
        if self.issue_width == 1:
            return 1.0
        elif self.issue_width == 2:
            return 2.5
        elif self.issue_width == 3:
            return 5.0
        elif self.issue_width == 4:
            return 7.5
        return 10.0

    def rf_misc_read_act_energy(self):
        return 3.5 + self.rf_port_energy()

    def rf_misc_write_act_energy(self):
        return 5.5 + self.rf_port_energy()

    def rf_fp_read_act_energy(self):
        return 5.5 + self.rf_port_energy()

    def rf_fp_write_act_energy(self):
        return 7.5 + self.rf_port_energy()

    def rf_int_read_act_energy(self):
        return 2.0 + self.rf_port_energy()

    def rf_int_write_act_energy(self):
        return 3.0 + self.rf_port_energy()


class RF_PowerOff:
    def __init__(self):
        super(RF_PowerOff, self).__init__()
        self.dyn = lambda: 0.0
        self.st = lambda: 0.0


class RF_PowerModel:
    def __init__(self, core, **kwargs):
        super(RF_PowerModel, self).__init__(**kwargs)
        # Choose a power model for every power state
        self.pm = [
            RF_PowerOn(core),  # ON
            RF_PowerOff(),  # CLK_GATED
            RF_PowerOff(),  # SRAM_RETENTION
            RF_PowerOff(),  # OFF
        ]
