from m5.objects import BaseMinorCPU

# Only import things you need

# Never use `import *`
from .base_power_model import AbstractPowerModel


class MinorRFPower(AbstractPowerModel):
    # avoid the use of default values
    def __init__(self, minorcpu: BaseMinorCPU, issue_width: int):
        super().__init__(minorcpu)
        # _rf isn't really needed since you have `_simobj`
        self._issue_width = issue_width
        self.name = "RF"

    def static_power(self) -> float:
        """Returns static power in Watts"""
        return 1.0

    def dynamic_power(self) -> float:
        energy = self.int_energy() + self.fp_energy() + self.misc_energy()
        print(
            f"{self.int_energy()} + {self.fp_energy()} + {self.misc_energy()}"
        )
        return self.convert_to_watts(energy)

    def int_energy(self) -> float:
        reads = self.get_stat("executeStats0.numIntRegReads")
        writes = self.get_stat("executeStats0.numIntRegWrites")
        return (
            reads * self.rf_int_read_act_energy()
            + writes * self.rf_int_write_act_energy()
        )

    def fp_energy(self) -> float:
        reads = self.get_stat("executeStats0.numFpRegReads")
        writes = self.get_stat("executeStats0.numFpRegWrites")

        return (
            reads * self.rf_fp_read_act_energy()
            + writes * self.rf_fp_write_act_energy()
        )

    def misc_energy(self) -> float:
        reads = self.get_stat("executeStats0.numMiscRegReads")
        writes = self.get_stat("executeStats0.numMiscRegWrites")

        return (
            reads * self.rf_misc_read_act_energy()
            + writes * self.rf_misc_write_act_energy()
        )

    def rf_port_energy(self) -> float:
        if self._issue_width == 1:
            return 1.0
        elif self._issue_width == 2:
            return 2.5
        elif self._issue_width == 3:
            return 5.0
        elif self._issue_width == 4:
            return 7.5
        return 10.0

    def rf_misc_read_act_energy(self) -> float:
        return 3.5 + self.rf_port_energy()

    def rf_misc_write_act_energy(self) -> float:
        return 5.5 + self.rf_port_energy()

    def rf_fp_read_act_energy(self) -> float:
        return 5.5 + self.rf_port_energy()

    def rf_fp_write_act_energy(self) -> float:
        return 7.5 + self.rf_port_energy()

    def rf_int_read_act_energy(self) -> float:
        return 2.0 + self.rf_port_energy()

    def rf_int_write_act_energy(self) -> float:
        return 3.0 + self.rf_port_energy()
