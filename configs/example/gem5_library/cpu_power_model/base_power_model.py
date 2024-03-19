# don't use `import *` and import the specific things you need
from m5.objects import Root

# Class names should be in CamelCase. Also, this is an abstract base class.
# I.e., no one should ever create an instance of this class.
class AbstractPowerModel:
    def __init__(self, simobj):
        # You shouldn't use a list of functions. Instead, implement the
        # dynamic/static power functions in the sub classes
        # using a leading underscore is a good idea so that it's not
        # considered a simobject child
        self._simobj = simobj
        self.name = "AbstractPowerModel"  # should be overridden for debugging
        # I don't like the above, but it's a little hack to make the debugging
        # easier.

    # A better name is "get_stat"
    def get_stat(self, stat):
        """Get the value of a stat, if it exists. Otherwise, return 0.0"""
        try:
            total = self._simobj.resolveStat(stat).total
            return total
        except:
            # In the future, this should be a `panic`
            print(f"{stat} not found in stats!")
            return 0.0

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
