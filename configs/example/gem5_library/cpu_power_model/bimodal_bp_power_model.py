from m5.objects import PowerModel, PowerModelFunc
from m5.stats import *
from .base_power_model import *


class BimodalBP_PowerOn(Base_PowerModel):
    def __init__(self, simobj):
        super(BimodalBP_PowerOn, self).__init__(simobj)
        self._bp = self.simobj
        self.dyn_fns = [
            self.predict_energy,
            self.mispredict_energy,
            self.update_energy,
        ]

    def static_power(self):
        """Returns static power in Watts"""
        return 1.0

    def predict_energy(self):
        return self.total_energy_btb_hits() + self.total_energy_cond_predict()

    def mispredict_energy(self):
        return (
            self.total_energy_btb_misses()
            + self.total_energy_cond_mispredict()
        )

    def update_energy(self):
        btb_updates = self.validate_stat("branchPred.BTBUpdates")
        return btb_updates * self.btb_update_act_energy()

    def total_energy_btb_hits(self):
        btb_hits = self.validate_stat("branchPred.BTBHits")
        return btb_hits * self.btb_hits_act_energy()

    def total_energy_btb_misses(self):
        btb_hits = self.validate_stat("branchPred.BTBHits")
        btb_lookups = self.validate_stat("branchPred.BTBLookups")
        btb_misses = btb_lookups - btb_hits
        return btb_misses * self.btb_misses_act_energy()

    def total_energy_cond_predict(self):
        cond_mispredicts = self.validate_stat("branchPred.condIncorrect")
        cond_predictions = self.validate_stat("branchPred.condPredicted")
        cond_correct = cond_mispredicts - cond_predictions

        return cond_mispredicts * self.cond_predict_act_energy()

    def total_energy_cond_mispredict(self):
        cond_mispredicts = self.validate_stat("branchPred.condIncorrect")
        return cond_mispredicts * self.cond_mispredict_act_energy()

    def btb_hits_act_energy(self):
        return 3.5

    def btb_misses_act_energy(self):
        return 10.5

    def cond_predict_act_energy(self):
        return 4.5

    def cond_mispredict_act_energy(self):
        return 8.5

    def btb_update_act_energy(self):
        return 7.5


"""
class BimodalBP_PowerOff(PowerModelFunc):
    def __init__(self):
        super(BimodalBP_PowerOff, self).__init__()
        self.dyn = lambda: 0.0
        self.st = lambda: 0.0


class BimodalBP_PowerModel(PowerModel):
    def __init__(self, bimodal_bp, **kwargs):
        super(BimodalBP_PowerModel, self).__init__(**kwargs)
        # Choose a power model for every power state
        self.pm = [
            BimodalBP_PowerOn(bimodal_bp),  # ON
            BimodalBP_PowerOff(),  # CLK_GATED
            BimodalBP_PowerOff(),  # SRAM_RETENTION
            BimodalBP_PowerOff(),  # OFF
        ]
"""
