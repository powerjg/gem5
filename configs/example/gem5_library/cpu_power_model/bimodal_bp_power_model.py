from m5.objects import BiModeBP

from .base_power_model import AbstractPowerModel


class BimodalBPPower(AbstractPowerModel):
    def __init__(self, branch_pred: BiModeBP):
        super().__init__(branch_pred)
        self.name = "BP"

    def static_power(self) -> float:
        """Returns static power in Watts"""
        return 1.0

    def dynamic_power(self) -> float:
        energy = (
            self.predict_energy()
            + self.mispredict_energy()
            + self.update_energy()
        )
        return self.convert_to_watts(energy)

    def predict_energy(self) -> float:
        return self.total_energy_btb_hits() + self.total_energy_cond_predict()

    def mispredict_energy(self) -> float:
        return (
            self.total_energy_btb_misses()
            + self.total_energy_cond_mispredict()
        )

    def update_energy(self) -> float:
        # btb_updates = self.get_stat("branchPred.BTBUpdates")
        btb_updates = self.get_stat("branchPred.BTBUpdates")
        return btb_updates * self.btb_update_act_energy()

    def total_energy_btb_hits(self) -> float:
        btb_hits = self.get_stat("branchPred.BTBHits")
        return btb_hits * self.btb_hits_act_energy()

    def total_energy_btb_misses(self) -> float:
        # btb_hits = self.get_stat("branchPred.BTBHits")
        # btb_lookups = self.get_stat("branchPred.BTBLookups")
        btb_hits = self.get_stat("branchPred.BTBHits")
        btb_lookups = self.get_stat("branchPred.BTBLookups")
        btb_misses = btb_lookups - btb_hits
        return btb_misses * self.btb_misses_act_energy()

    def total_energy_cond_predict(self) -> float:
        cond_mispredicts = self.get_stat("branchPred.condMispredicted")
        cond_predictions = self.get_stat("branchPred.condPredicted")
        cond_correct = cond_mispredicts - cond_predictions

        return cond_mispredicts * self.cond_predict_act_energy()

    def total_energy_cond_mispredict(self) -> float:
        cond_mispredicts = self.get_stat("branchPred.condMispredicted")
        return cond_mispredicts * self.cond_mispredict_act_energy()

    def btb_hits_act_energy(self) -> float:
        return 3.5

    def btb_misses_act_energy(self) -> float:
        return 10.5

    def cond_predict_act_energy(self) -> float:
        return 4.5

    def cond_mispredict_act_energy(self) -> float:
        return 8.5

    def btb_update_act_energy(self) -> float:
        return 7.5
