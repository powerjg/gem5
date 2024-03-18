from m5.objects import PowerModel, PowerModelFunc
from m5.stats import *


class BimodalBP_PowerOn(PowerModelFunc):
    def __init__(self, bimodal_bp):
        super(BimodalBP_PowerOn, self).__init__()
        self._bp = bimodal_bp
        self.dyn = lambda: self.dynamic_power()
        self.st = lambda: self.static_power()

    def static_power(self):
        """Returns static power in Watts"""
        return 1.0

    def dynamic_power(self):
        """Returns dynamic power in Watts"""
        time = Root.getInstance().resolveStat("simSeconds").total
        total_energy_nj = self.predict_energy() + self.mispredict_energy()
        total_energy_j = total_energy_nj * 1e-9
        return total_energy_j / time

    def predict_energy(self):
        return self.total_energy_btb_hits() + self.total_energy_cond_predict()

    def mispredict_energy(self):
        return (
            self.total_energy_btb_misses()
            + self.total_energy_cond_mispredict()
        )

    def total_energy_btb_hits(self):
        btb_hits = self._bp.resolveStat("branchPred.BTBHits").total
        return btb_hits * self.btb_hits_act_energy()

    def total_energy_btb_misses(self):
        btb_lookups = self._bp.resolveStat("branchPred.BTBLookups").total
        btb_hits = self._bp.resolveStat("branchPred.BTBHits").total
        btb_misses = btb_lookups - btb_hits

        return btb_misses * self.btb_misses_act_energy()

    def total_energy_cond_predict(self):
        cond_mispredicts = self._bp.resolveStat(
            "branchPred.condIncorrect"
        ).total
        cond_predictions = self._bp.resolveStat(
            "branchPred.condPredicted"
        ).total
        cond_correct = cond_mispredicts - cond_predictions

        return cond_mispredicts * self.cond_predict_act_energy()

    def total_energy_cond_mispredict(self):
        cond_mispredicts = self._bp.resolveStat(
            "branchPred.condIncorrect"
        ).total
        return cond_mispredicts * self.cond_mispredict_act_energy()

    def btb_hits_act_energy(self):
        return 3.5

    def btb_misses_act_energy(self):
        return 10.5

    def cond_predict_act_energy(self):
        return 4.5

    def cond_mispredict_act_energy(self):
        return 8.5


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
