//
// Created by Plutex on 1/4/26.
//

#ifndef PLUENGINE_PYTHONMANAGER_H
#define PLUENGINE_PYTHONMANAGER_H

#include "PluEngine/Core.h"
#include "PluSTL/PluSTL_FWD.h"
#include "PythonManager.generated.h"
#include "PluBase/PluUUID.h"
#include "PluEngine/Core/Objects/EngineObject.h"

namespace Plu
{
	PLU_CLASS(Abstract)
	class PLUSCRIPTING_API IPythonManager : public EngineObject
	{
		REFLECTION_BODY_IPYTHONMANAGER()
	public:
		virtual bool RunScript(PluUUID uuid) = 0;
		virtual bool RunScript(PathW path, PathW workDir, String args) = 0;
	};
}

#endif //PLUENGINE_PYTHONMANAGER_H