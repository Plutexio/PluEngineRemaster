//
// Created by Plutex on 11/11/26.
//

#include "PluEngine/Scripting/EmbeddedPython.h"

#include <cstring>
#include <stdexcept>
#include <string>

#include "EmbeddedPythonFiles.h"
#include "PluEngine/Timer.h"

namespace
{
	const Plu::Detail::EmbeddedPythonFile* FindEmbeddedPythonFile(const char* name)
	{
		for (const Plu::Detail::EmbeddedPythonFile* file = Plu::Detail::gEmbeddedPythonFiles; file->Name; ++file) {
			if (std::strcmp(file->Name, name) == 0) return file;
		}
		return nullptr;
	}
}

pybind11::dict Plu::ExecEmbeddedPython(const char* name, const char* moduleName)
{
	PLU_PROFILE_SCOPE("ExecEmbeddedPython");
	const Detail::EmbeddedPythonFile* file = FindEmbeddedPythonFile(name);
	if (!file) {
		throw std::runtime_error(std::string("No embedded Python file named '") + name + "'");
	}

	pybind11::module_ builtins = pybind11::module_::import("builtins");
	// Only shows in tracebacks, where it points at the file the code was compiled from.
	const std::string fileName = std::string("PluScripting/python/") + name + ".py";

	pybind11::dict ns;
	ns["__name__"] = moduleName;
	ns["__file__"] = fileName;
	ns["__builtins__"] = builtins;
	pybind11::object code = builtins.attr("compile")(pybind11::str(reinterpret_cast<const char*>(file->Source)), fileName, "exec");
	builtins.attr("exec")(code, ns);
	return ns;
}

void Plu::InstallPythonUtils(pybind11::module_& module)
{
	PLU_PROFILE_SCOPE("InstallPythonUtils");
	// Run under the name "PluEngine", so what it defines reports PluEngine as its module (repr,
	// pickling), just like the C++ bindings next to it.
	pybind11::dict ns = ExecEmbeddedPython("PluEngineUtils", "PluEngine");
	for (pybind11::handle name : ns["__all__"]) {
		module.attr(name) = ns[name];
	}
}

void Plu::RedirectPythonOutputToLog()
{
	try {
		pybind11::dict ns = ExecEmbeddedPython("PluLogRedirect", "PluEngine.LogRedirect");
		ns["Install"](pybind11::cpp_function([](const std::string& message, bool isError) {
			if (isError) PLU_ERROR("[Python] {}", message);
			else PLU_INFO("[Python] {}", message);
		}));
	} catch (const std::exception& e) {
		PLU_CORE_ERROR("Could not redirect Python output to the log: {}", e.what());
	}
}
