//
// Created by Plutex on 2026-08-11.
//

#ifndef PLUENGINE_PYTHONMATH_H
#define PLUENGINE_PYTHONMATH_H

#include <pybind11/pybind11.h>

#include "PluEngine/Core.h"

namespace Plu
{
	// Registers the glm math types (Vec2/3/4, IVec2/3/4, UVec2/3/4, Quaternion, Matrix4) as pybind11 classes.
	//
	// Called by the generated PluEngineBindings.cpp as the very first thing in the module, before
	// enums and classes: pybind11 resolves parameter types, return types and default arguments at
	// .def() time, so anything using a math type has to be registered after this.
	void PLUSCRIPTING_API RegisterMathTypes(pybind11::module_& module);
}

#endif //PLUENGINE_PYTHONMATH_H
