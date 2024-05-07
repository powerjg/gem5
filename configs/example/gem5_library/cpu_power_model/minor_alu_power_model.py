from m5.objects import BaseMinorCPU
from .base_power_model import AbstractPowerModel


class MinorALUPower(AbstractPowerModel):
    def __init__(self, minorcpu: BaseMinorCPU):
        super().__init__(minorcpu)
        self.name = "ALU"
        self._cpu = minorcpu

    def static_power(self) -> float:
        # no need for documentation if you're overriding the base class
        return 1.0

    def dynamic_power(self) -> float:
        print(
            f"{self.int_energy()} + {self.fp_energy()} + {self.vec_energy()}"
        )
        energy = self.int_energy() + self.fp_energy() + self.vec_energy()
        return self.convert_to_watts(energy)

    def int_energy(self) -> float:
        # Note: the stat below doesn't exist
        int_accesses = self.get_stat("executeStats0.numIntAluAccesses")
        return int_accesses * self.int_alu_act_energy()

    def fp_energy(self) -> float:
        fp_accesses = self.get_stat("executeStats0.numFpAluAccesses")
        return fp_accesses * self.fp_alu_act_energy()

    def vec_energy(self) -> float:
        vec_accesses = self.get_stat("executeStats0.numVecAluAccesses")
        return vec_accesses * self.vec_alu_act_energy()

    def fp_alu_act_energy(self) -> float:
        return 3.5

    def int_alu_act_energy(self) -> float:
        return 1.0

    def vec_alu_act_energy(self) -> float:
        return 2.5
