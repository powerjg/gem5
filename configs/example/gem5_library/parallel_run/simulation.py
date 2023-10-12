import importlib
import inspect
from itertools import product
import os
import signal
from time import sleep

from gem5.utils.multiprocessing import Process


class Simulation:
    def __init__(self):
        from gem5.simulate.simulator import Simulator

        board = self.get_board()
        self.simulator = Simulator(board=board)

    def run(self):
        self.simulator.run()

    @classmethod
    def get_name(cls, workload):
        return f"simulation/{workload.get_id()}"


def run_sim(simulation, *args, **kwargs):
    sim = simulation(*args, **kwargs)
    sim.run()


def get_simulations(script):

    script_module = importlib.import_module(script)

    simulation_classes = []
    for _, obj in inspect.getmembers(script_module):
        if (
            inspect.isclass(obj)
            and issubclass(obj, Simulation)
            and not obj == Simulation
        ):
            simulation_classes.append(obj)

    return simulation_classes


def run_all(simulation_classes, suite, num_cpus):
    processes = []
    print(f"Starting {len(simulation_classes)*len(suite)} simulations")
    for workload, simulation in product(suite, simulation_classes):
        if len(processes) > num_cpus:
            for process in processes:
                if not process.is_alive():
                    processes.remove(process)
                os.kill(process.pid, signal.SIGTERM)
            sleep(1)
        print(f"Starting {simulation.get_name(workload)}")
        process = Process(
            target=run_sim,
            args=(simulation, workload),
            name=simulation.get_name(workload),
        )
        process.start()
        processes.append(process)

    while processes:
        for process in processes:
            if not process.is_alive():
                processes.remove(process)
        sleep(1)
    print("All processes completed")
