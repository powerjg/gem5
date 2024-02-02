from m5.SimObject import SimObject
from m5.params import *
from m5.objects.PowerModelState import PowerModelState

# Dynamic and static power equations represented by arithmetic operators than strings in MathExprPowerModel
class PowerModelFunc(PowerModelState):
    type = "PowerModelFunc"
    cxx_header = "learning_gem5/part2/power_model_func.hh"
    cxx_class = "gem5::PowerModelFunc"

    # Equations for dynamic and static power in Watts
    # Equations may use gem5 stats ie. "1.1*ipc + 2.3*l2_cache.overall_misses"
    # It is possible to use automatic variables such as "temp"
    # You may also use stat names (relative path to the simobject)
    dyn = Param.PyFunc("Function to call for Dynamic Power")
    stat = Param.PyFunc("Function to call for Static Power")
