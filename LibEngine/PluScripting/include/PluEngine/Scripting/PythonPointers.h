//
// Created by Plutex on 2026-02-22.
//

#ifndef PLUENGINE_PYTHONPOINTERS_H
#define PLUENGINE_PYTHONPOINTERS_H

#include <pybind11/pybind11.h>
#include "Pointers/ControlBlock.h"
#include "PluEngine/Core/Reflection/ClassPointer.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"

namespace Plu
{
	template<typename T>
	struct PyControlBlock : ControlBlock<T>
	{
		pybind11::object pyKeeper;

		PyControlBlock(T* raw, pybind11::object keeper)
			: ControlBlock<T>(raw), pyKeeper(std::move(keeper))
		{
			this->isPython = true;
		}
	};

	template<typename B>
	requires EngineObjectConc<B>
	TOwningPointer<B> OwnerFromPython(pybind11::type type)
	{
		pybind11::object obj = type();   // wywołaj konstruktor Pythona
		B* raw = obj.cast<B*>();
		TOwningPointer<B> result;
		result.control = new PyControlBlock<B>(raw, std::move(obj));
		std::string name = pybind11::str(type.attr("__name__"));
		TypeInfo* newType = TypeRegistry::GetInstance()->GetTypeOfName(name.c_str());
		if (!newType) {
			PLU_CORE_ERROR("Invalid Type for python object!");
			return nullptr;
		}
		result.GetRaw()->mPythonType = newType;
		return result;
	}

	// Rebuilds a TUsePointer from a raw engine object handed over by Python. pybind11 has no caster
	// for the engine smart pointers, so Python only ever holds raw pointers; generated bindings of
	// functions taking TUsePointer<T> accept T* and recover the pointer through the object's handle.
	// Null in, null out — also null when the object is no longer alive.
	template<typename T>
	requires EngineObjectConc<T>
	TUsePointer<T> UserFromPython(T* raw)
	{
		if (!raw) return nullptr;
		return TypeRegistry::GetInstance()->GetObjectManager()->GetObjectAsUser<T>(raw->GetObjectHandle());
	}
}
#endif //PLUENGINE_PYTHONPOINTERS_H
