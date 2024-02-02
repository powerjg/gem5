#include "learning_gem5/part2/power_model_func.hh"

#include <iostream>

namespace gem5{

PowerModelFunc::PowerModelFunc(const Params &p)  
	   :  PowerModelState(p), dyn(p.dyn), stat(p.stat)
	{
	   // Bind PyFunc parameters into functions to be called in this SimObj
	   pybind11::function dyn_func = pybind11::reinterpret_borrow<pybind11::function>(dyn);
	   pybind11::function stat_func = pybind11::reinterpret_borrow<pybind11::function>(stat);

	   // Call the functions
	   dyn_func();
	   stat_func();
	}

} // namespace gem5



