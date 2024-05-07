from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.classic.private_l1_cache_hierarchy import (
    PrivateL1CacheHierarchy,
)
from gem5.components.memory.single_channel import SingleChannelDDR3_1600
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.components.processors.cpu_types import CPUTypes
from gem5.resources.resource import Resource, BinaryResource, CustomResource
from gem5.resources.workload import CustomWorkload
from gem5.simulate.simulator import Simulator
from m5.stats.gem5stats import get_simstat
from m5.objects import *
from m5.objects.BranchPredictor import TAGE
from m5.stats import *

from l1l2_cache_with_pm.l1l2_cache_with_pm import (
    PrivateL1SharedL2CacheHierarchy,
)

from cpu_power_model.my_in_order_processor import MyInOrderProcessor

# Use below for debugging:
# import pdb; pdb.set_trace()

# Obtain the components.
cache_hierarchy = PrivateL1SharedL2CacheHierarchy(
    l1d_size="32kB", l1i_size="32kB", l2_size="64KiB"
)
memory = SingleChannelDDR3_1600("1GiB")
processor = MyInOrderProcessor()

# Add them to the board.
board = SimpleBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)
# Set the workload.
binary = Resource("x86-hello64-static")
board.set_se_binary_workload(binary)

# Setup the Simulator and run the simulation.
simulator = Simulator(board=board)
simulator.run()
