#ifndef __SIM_POWERMODEL_FUNC_PM_HH__
#define __SIM_POWERMODEL_FUNC_PM_HH__

#include "params/PowerModelFunc.hh"
#include "sim/sim_object.hh"
#include "python/pybind11/pybind.hh"
#include "sim/power/power_model.hh"

namespace gem5
{

class PowerModelFunc : public PowerModelState
{
   public:
     typedef PowerModelFuncParams Params;
     PowerModelFunc(const Params &p);
     //void startup() override;
     double getDynamicPower() const override { return dyn_func.cast<double>(); } 
     double getStaticPower() const override { return st_func.cast<double>(); } 

   private:
     pybind11::object dyn;
     pybind11::object st;
     pybind11::function dyn_func;
     pybind11::function st_func;
};

} // namespace gem5

#endif
