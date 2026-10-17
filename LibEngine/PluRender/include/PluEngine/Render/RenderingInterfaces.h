//
// Created by Plutex on 1/19/26.
//

#ifndef PLUENGINE_RENDERINGINTERFACES_H
#define PLUENGINE_RENDERINGINTERFACES_H

#include "PluEngine/Core/PluInterface.h"
#include "RenderingInterfaces.generated.h"
#include "PluEngine/PluTypes.h"

namespace Plu
{
	struct MaterialInfo;
	struct StaticMesh;
}

namespace Plu
{
	class ShaderProgram;
}

namespace Plu
{
	PLU_ENUM(PyExport, PyNamespace=Plu)
	enum class PerspectiveType
	{
		Perspective,
		Orthographic
	};

	PLU_STRUCT(PyExport)
	struct PLURENDER_API CameraOptions
	{
		REFLECTION_BODY_CAMERAOPTIONS()

		PLU_PROPERTY(PyExport)
		PerspectiveType CameraPerspective;

		PLU_PROPERTY(PyExport)
		float OrthoWidth = 100;

		PLU_PROPERTY(PyExport)
		float FieldOfView = 45;
	};

	PLU_INTERFACE()
	class IRendererCamera
	{
	public:
		virtual ~IRendererCamera() = default;
		virtual CameraOptions* GetCameraOptions() = 0;
		virtual Vec3 GetCameraLocation() = 0;
		virtual Vec3 GetCameraRotation() = 0;
	};
}

#endif //PLUENGINE_RENDERINGINTERFACES_H