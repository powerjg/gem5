from gem5.components.cachehierarchies.classic.abstract_classic_cache_hierarchy import (
    AbstractClassicCacheHierarchy,
)
from gem5.components.boards.abstract_board import AbstractBoard

from .l1cache import L1Cache
from .l2cache import L2Cache

import m5
from .power_model import *
from m5.stats.gem5stats import get_simstat


class PrivateL1SharedL2CacheHierarchy(AbstractClassicCacheHierarchy):
    def __init__(self, l1i_size: str, l1d_size: str, l2_size: str) -> None:
        AbstractClassicCacheHierarchy.__init__(self=self)
        self.membus = SystemXBar(width=64)
        self._l1i_size = l1i_size
        self._l1d_size = l1d_size
        self._l2_size = l2_size

    def get_mem_side_port(self) -> Port:
        return self.membus.mem_side_ports

    def get_cpu_side_port(self) -> Port:
        return self.membus.cpu_side_ports

    def incorporate_cache(self, board: AbstractBoard) -> None:
        # Set up the system port for functional access from the simulator.
        board.connect_system_port(self.membus.cpu_side_ports)

        for cntr in board.get_memory().get_memory_controllers():
            cntr.port = self.membus.mem_side_ports

        self.l1icaches = [
            L1Cache(size=self._l1i_size)
            for i in range(board.get_processor().get_num_cores())
        ]

        self.l1dcaches = [
            L1Cache(size=self._l1d_size)
            for i in range(board.get_processor().get_num_cores())
        ]

        self.l2cache = L2Cache(size=self._l2_size)

        self.l2XBar = L2XBar()

        # From the PM tutorial, below:
        for l2 in self.l2cache.descendants():
            if not isinstance(l2, m5.objects.Cache):
                continue
            l2.power_state.default_state = "ON"

            # Line below causes an error, related to it being an orphan?
            l2.power_model = L2PowerModel(l2.data_latency)
        # TODO break below into data and tag PMs
        # self.l2cache.power_model = L2PowerModel()
        # ^ can't change path later (part of TODO: leave hard-code in)
        # TODO: python script to sum energies in stats.txt
        # check if multiple power models within a simobj?
        # TODO define py expression instead of string instead, see if numbers match, pass a py function, call from c++

        for i, cpu in enumerate(board.get_processor().get_cores()):

            cpu.connect_icache(self.l1icaches[i].cpu_side)
            cpu.connect_dcache(self.l1dcaches[i].cpu_side)

            self.l1icaches[i].mem_side = self.l2XBar.cpu_side_ports
            self.l1dcaches[i].mem_side = self.l2XBar.cpu_side_ports

            int_req_port = self.membus.mem_side_ports
            int_resp_port = self.membus.cpu_side_ports
            cpu.connect_interrupt(int_req_port, int_resp_port)

        self.l2XBar.mem_side_ports = self.l2cache.cpu_side

        self.membus.cpu_side_ports = self.l2cache.mem_side
