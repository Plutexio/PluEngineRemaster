//
// Created by Plutex on 2026-03-26.
//

#ifndef PLUENGINE_EDITORCAMERA_H
#define PLUENGINE_EDITORCAMERA_H
#include "PluEngine/Core.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "EditorCamera.generated.h"
#include "PluEngine/Gameplay/Components/CameraComponent.h"

namespace Plu
{
	PLU_CLASS()
	class EditorSceneCamera : public EngineObject, public ICamera
	{
		REFLECTION_BODY_EDITORSCENECAMERA()
	private:
		Vec3 mRotation;
		Vec3 mNiceRotation;
		Vec3 mLocation;
		float mMoveSpeed = 15;
		CameraOptions mCameraOptions;
	public:
		EditorSceneCamera();
		~EditorSceneCamera() override = default;

		void OnUpdate(float deltaTime);

		void SetCameraLocation(Vec3 location);
		void SetCameraOptions(CameraOptions *cameraOptions);
		void SetCameraRotation(Vec3 rotation);

		Vec3 GetHumanReadableRotation();
		Vec3 GetCameraLocation() override;
		CameraOptions *GetCameraOptions() override;
		Vec3 GetCameraRotation() override;
	};
}

#endif //PLUENGINE_EDITORCAMERA_H
