from m5.stats import *


class Base_PowerModel:
    def __init__(self, simobj):
        self.dyn_fns = []
        self.st_fns = []
        self.simobj = simobj

    def validate_stat(self, stat):
        try:
            total = self.simobj.resolveStat(stat).total
            return total
        except:
            print(f"{stat} not found in stats!")
            return 0.0

    def dynamic_power(self):
        total = 0.0
        if self.dyn_fns:
            for fn in self.dyn_fns:
                total += fn()
        return self.convert_to_watts(total)

    def static_power(self):
        total = 0.0
        if self.st_fns:
            for fn in self.st_fns:
                total += fn()
        return self.convert_to_watts(total)

    def convert_to_watts(self, value):
        time = Root.getInstance().resolveStat("simSeconds").total
        value_in_j = value * 1e-9
        return value_in_j / time
