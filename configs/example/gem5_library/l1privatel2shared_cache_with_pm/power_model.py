
from m5.objects import SystemXBar, Port, L2XBar, MathExprPowerModel, PowerModel

class L2PowerOn(MathExprPowerModel):
    def __init__(self, l2_path, **kwargs):
        super(L2PowerOn, self).__init__(**kwargs)
        # Example to report l2 Cache overallAccesses
        # The estimated power is converted to Watt and will vary based
        # on the size of the cache
        
        #line below throws an error if you pass it in l2.path()
        #self.dyn = f"{l2_path}.overallAccesses *  0.000025000"
        self.st = "(voltage * 3)/10"
        self.dyn = f"board.cache_hierarchy.l2cache.overallAccesses * 0.00001800"


class L2PowerOff(MathExprPowerModel):
    dyn = "0"
    st = "0"


class L2PowerModel(PowerModel):
    def __init__(self, l2_path, **kwargs):
        super(L2PowerModel, self).__init__(**kwargs)
        # Choose a power model for every power state
        self.pm = [
                L2PowerOn(l2_path),  # ON
                L2PowerOff(),  # CLK_GATED
                L2PowerOff(),  # SRAM_RETENTION
                L2PowerOff(),  # OFF
                ]
