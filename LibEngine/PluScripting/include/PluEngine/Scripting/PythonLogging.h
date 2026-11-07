//
// Created by Plutex on 2026-02-25.
//

#ifndef PLUENGINE_PYTHONLOGGING_H
#define PLUENGINE_PYTHONLOGGING_H

#include "PluSTL/PluSTL_FWD.h"
#include "PluEngine/Core.h"
#include "PluBase/Log.h"

namespace Plu
{
	PLU_FUNCTION()
	inline void PrintTrace(String msg)
	{
		PLU_CORE_TRACE("[PYTHON] {}", msg.CStr());
	}

	PLU_FUNCTION()
	inline void PrintLog(String msg)
	{
		PLU_CORE_INFO("[PYTHON] {}", msg.CStr());
	}

	PLU_FUNCTION()
	inline void PrintWarn(String msg)
	{
		PLU_CORE_WARN("[PYTHON] {}", msg.CStr());
	}

	PLU_FUNCTION()
	inline void PrintError(String msg)
	{
		PLU_CORE_ERROR("[PYTHON] {}", msg.CStr());
	}
}

#endif //PLUENGINE_PYTHONLOGGING_H