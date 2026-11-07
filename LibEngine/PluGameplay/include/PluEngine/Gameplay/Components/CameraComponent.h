//
// Created by Plutex on 1/19/26.
//

#ifndef PLUENGINE_CAMERACOMPONENT_H
#define PLUENGINE_CAMERACOMPONENT_H
#include "PluEngine/Gameplay/WorldComponent.h"
#include "CameraComponent.generated.h"
#include "PluEngine/Core/BoundingBox.h"
#include "PluEngine/Render/RenderingInterfaces.h"

namespace Plu
{
	PLU_CLASS(PyExport)
	class PLUGAMEPLAY_API CameraComponent final : public WorldComponent, public IRendererCamera
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