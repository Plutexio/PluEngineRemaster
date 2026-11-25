//
// Created by Plutex on 1/19/26.
//

#ifndef PLUENGINE_CAMERACOMPONENT_H
#define PLUENGINE_CAMERACOMPONENT_H
#include "PluEngine/Gameplay/WorldComponent.h"
#include "CameraComponent.generated.h"
#include "PluEngine/Core/BoundingBox.h"

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
	class ICamera
	{
	public:
		virtual ~ICamera() = default;
		virtual CameraOptions* GetCameraOptions() = 0;
		virtual Vec3 GetCameraLocation() = 0;
		virtual Vec3 GetCameraRotation() = 0;
	};

	PLU_CLASS(PyExport)
	class PLUGAMEPLAY_API CameraComponent final : public WorldComponent, public ICamera
	{
		REFLECTION_BODY_CAMERACOMPONENT()
	public:
		CameraComponent() = default;
		~CameraComponent() override = default;

		void OnUpdate(float deltaTime) override;
		void OnBeginPlay() override;
		void OnEndPlay() override;

		PLU_PROPERTY(PyExport)
		CameraOptions Options;

		Vec3 GetCameraLocation() override;
		CameraOptions *GetCameraOptions() override;
		Vec3 GetCameraRotation() override;
	};
}

#endif //PLUENGINE_CAMERACOMPONENT_H