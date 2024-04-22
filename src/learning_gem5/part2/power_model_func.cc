#include "learning_gem5/part2/power_model_func.hh"

#include "base/trace.hh"
#include "debug/PowerModelFunc.hh"

namespace gem5{

PowerModelFunc::PowerModelFunc(const Params &p)
           :  PowerModelState(p), dyn(p.dyn), st(p.st)
        {
           // Bind PyFunc parameters into functions to be called in this SimObj

           dyn_func =
                   pybind11::reinterpret_borrow<pybind11::function>(dyn);
           st_func =
                   pybind11::reinterpret_borrow<pybind11::function>(st);
        }
/*
void
PowerModelFunc::startup()
{

}
*/
} // namespace gem5
