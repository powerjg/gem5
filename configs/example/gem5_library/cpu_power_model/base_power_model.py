# don't use `import *` and import the specific things you need
from m5.objects import Root
from m5.objects import BaseMinorCPU, BaseO3CPU, BaseSimpleCPU

bp_stats = {
    "BTBLookups": "branchPred.BTBLookups",
    "BTBHits": "branchPred.BTBHits",
    "BTBUpdates": "branchPred.BTBUpdates",
    "condPredicted": "branchPred.condPredicted",
    "condMispredicted": "branchPred.condIncorrect",
}

minor_stats = {
    "numIntAluAccesses": "executeStats0.numIntAluAccesses",
    "numFpAluAccesses": "executeStats0.numFpAluAccesses",
    "numVecAluAccesses": "executeStats0.numVecAluAccesses",
    "numIntRegReads": "executeStats0.numIntRegReads",
    "numIntRegWrites": "executeStats0.numIntRegWrites",
    "numFpRegReads": "executeStats0.numFpRegReads",
    "numFpRegWrites": "executeStats0.numFpRegWrites",
    "numMiscRegReads": "executeStats0.numFpRegReads",
    "numMiscRegWrites": "executeStats0.numFpRegWrites",
    "BTBLookups": bp_stats["BTBLookups"],
    "BTBHits": bp_stats["BTBHits"],
    "BTBUpdates": bp_stats["BTBUpdates"],
    "condPredicted": bp_stats["condPredicted"],
    "condMispredicted": bp_stats["condMispredicted"],
}

o3_stats = {
    "numIntAluAccesses": "intAluAccesses",
    "numFpAluAccesses": "fpAluAccesses",
    "numVecAluAccesses": "vecAluAccesses",
    "numIntRegReads": "executeStats0.numIntRegReads",
    "numIntRegWrites": "executeStats0.numIntRegWrites",
    "numFpRegReads": "executeStats0.numFpRegReads",
    "numFpRegWrites": "executeStats0.numFpRegWrites",
    "numMiscRegReads": "executeStats0.numFpRegReads",
    "numMiscRegWrites": "executeStats0.numFpRegWrites",
    "BTBLookups": bp_stats["BTBLookups"],
    "BTBHits": bp_stats["BTBHits"],
    "BTBUpdates": bp_stats["BTBUpdates"],
    "condPredicted": bp_stats["condPredicted"],
    "condMispredicted": bp_stats["condMispredicted"],
}


# Class names should be in CamelCase. Also, this is an abstract base class.
# I.e., no one should ever create an instance of this class.
class AbstractPowerModel:
    def __init__(self, simobj):
        # You shouldn't use a list of functions. Instead, implement the
        # dynamic/static power functions in the sub classes
        # using a leading underscore is a good idea so that it's not
        # considered a simobject child
        self._simobj = simobj

    # A better name is "get_stat"
    def get_stat(self, stat):
        """Get the value of a stat, if it exists. Otherwise, return 0.0"""
        print(f"Getting stat: {stat} for {self._simobj.name}")
        return self._simobj.resolveStat(stat).total

    def dynamic_power(self) -> float:
        """Returns dynamic power in Watts"""
        # These should not be implemented in this (abstract) base class
        raise NotImplementedError

    def static_power(self) -> float:
        """Returns static power in Watts"""
        # These should not be implemented in this (abstract) base class
        raise NotImplementedError

    def convert_to_watts(self, value: float) -> float:
        """Convert energy in nanojoules to Watts"""
        time = Root.getInstance().resolveStat("simSeconds").total
        value_in_j = value * 1e-9
        return value_in_j / time
