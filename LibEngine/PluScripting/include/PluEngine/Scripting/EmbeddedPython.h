//
// Created by Plutex on 11/11/26.
//

#ifndef PLUENGINE_EMBEDDEDPYTHON_H
#define PLUENGINE_EMBEDDEDPYTHON_H

#include <pybind11/pybind11.h>

#include "PluEngine/Core.h"

namespace Plu
{
	// The engine's own Python code lives as plain .py files in LibEngine/PluScripting/python/ and is
	// compiled into PluScripting at build time (cmake/PluEmbedPython.cmake). It needs no path at
	// runtime, no copy in the distribution and no PyArmor pass, and the editor and the runtime get
	// the same code.

	// Runs the embedded file `<name>.py` in a fresh namespace whose __name__ is `moduleName` and
	// returns that namespace. Throws pybind11::error_already_set when the code raises, and
	// std::runtime_error when no file of that name was embedded. Needs the GIL.
	PLUSCRIPTING_API pybind11::dict ExecEmbeddedPython(const char* name, const char* moduleName);

	// Merges the public names (__all__) of python/PluEngineUtils.py into the PluEngine module.
	// Called by the generated PluEngineBindings.cpp after every binding is registered.
	PLUSCRIPTING_API void InstallPythonUtils(pybind11::module_& module);

	// Sends sys.stdout and sys.stderr to the engine log (python/PluLogRedirect.py). Call once per
	// interpreter, right after Py_Initialize.
	PLUSCRIPTING_API void RedirectPythonOutputToLog();
}

#endif //PLUENGINE_EMBEDDEDPYTHON_H
