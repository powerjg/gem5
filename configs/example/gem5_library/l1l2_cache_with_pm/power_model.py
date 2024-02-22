from m5.objects import SystemXBar, Port, L2XBar, PowerModelFunc, PowerModel
from m5.stats.gem5stats import get_simstat


def dyn_eq(data_latency):
    # how to access L2 Cache stat? problem: this is also being called during initalizing the cache hierarchy
    return data_latency * 0.000180


def dyn_2():
    return 0.0


def st_eq():
    return 0.0


class L2PowerOn(PowerModelFunc):
    def __init__(self, data_latency, **kwargs):
        super(L2PowerOn, self).__init__(**kwargs)
        # Example to report l2 Cache overallAccesses
        # The estimated power is converted to Watt and will vary based
        # on the size of the cache

        # line below throws an error if you pass it in l2.path()
        # self.dyn = f"{l2_path}.overallAccesses *  0.000025000"
        # self.st = "(voltage * 3)/10"

        self.dyn = dyn_eq(data_latency)
        self.st = st_eq()
        # self.dyn = f"board.cache_hierarchy.l2cache.overallAccesses * 0.00001800"


class L2PowerOff(PowerModelFunc):
    dyn = st_eq()
    st = st_eq()


class L2PowerModel(PowerModel):
    def __init__(self, data_latency, **kwargs):
        super(L2PowerModel, self).__init__(**kwargs)
        # Choose a power model for every power state
        self.pm = [
            L2PowerOn(data_latency),  # ON
            L2PowerOff(),  # CLK_GATED
            L2PowerOff(),  # SRAM_RETENTION
            L2PowerOff(),  # OFF
        ]
