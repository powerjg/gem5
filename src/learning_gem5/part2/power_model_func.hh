#ifndef __SIM_POWERMODEL_FUNC_PM_HH__
#define __SIM_POWERMODEL_FUNC_PM_HH__

#include "params/PowerModelFunc.hh"
#include "python/pybind11/pybind.hh"
#include "sim/sim_object.hh"
#include "sim/power/power_model.hh"

namespace gem5
{

class PowerModelFunc : public PowerModelState
{
   public:
     typedef PowerModelFuncParams Params;
     PowerModelFunc(const Params &p);
   private:
     pybind11::object dyn;
     pybind11::object stat;
};

} // namespace gem5

#endif
