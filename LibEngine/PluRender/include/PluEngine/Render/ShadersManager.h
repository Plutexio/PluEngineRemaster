//
// Created by Plutex on 1/4/26.
//

#ifndef PLUENGINE_SHADERSMANAGER_H
#define PLUENGINE_SHADERSMANAGER_H
#include "PluEngine/Core/Objects/EngineObject.h"
#include "ShadersManager.generated.h"
#include "PluBase/PluUUID.h"

namespace Plu
{
	class IShaderCode;
	class ShaderProgram;
	PLU_CLASS(Abstract)
	class PLURENDER_API IShaderManager : public EngineObject
	{
		REFLECTION_BODY_ISHADERMANAGER()
	public:
		virtual void LoadShader(PluUUID uuid) = 0;
		virtual TUsePointer<ShaderProgram> GetShaderProgram(PluUUID uuid) = 0;
		virtual TUsePointer<IShaderCode> GetShaderCode(PluUUID uuid) = 0;
		virtual bool ShaderCodeExists(PluUUID uuid) = 0;
		virtual DynamicArray<TUsePointer<ShaderProgram>>* GetRenderableShaderPrograms() = 0;
		// Release render-owned GPU resources (shader programs). MUST be called on the render
		// thread (where the programs were created) before the GL context is released, so the
		// TOwningPointer thread-affinity assert doesn't fire when the manager is later destroyed
		// on the main thread. Default no-op for managers that don't own render-thread resources.
		virtual void ReleaseRenderResources() {}
	};
}

#endif //PLUENGINE_SHADERSMANAGER_H
