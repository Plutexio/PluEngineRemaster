//
// Created by Plutex on 2026-03-09.
//

#include "PluEngine/Gameplay/Components/PhysicsBoxColliderComponent.h"

#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/PluUtils.h"

Plu::PhysicsBoxColliderComponent::PhysicsBoxColliderComponent()
{
	BoxSize = {1,1,1};
}

void Plu::PhysicsBoxColliderComponent::SetBoxSize(Vec3 newBoxSize)
{
	BoxSize = newBoxSize;
	DispatchEvent("ShapeChanged", nullptr);
}

JPH::ShapeRefC Plu::PhysicsBoxColliderComponent::GetShape()
{
	float x = Plu::ClampF(BoxSize.x, 0.001f, FLT_MAX);
	float y = Plu::ClampF(BoxSize.y, 0.001f, FLT_MAX);
	float z = Plu::ClampF(BoxSize.z, 0.001f, FLT_MAX);

	static HashMap<Vec3, JPH::ShapeRefC> shapeCache;
	if (!shapeCache.Contains({x,y,z})) {
		shapeCache[{x,y,z}] = new JPH::BoxShape(JPH::Vec3(x, y, z));
		return shapeCache[{x,y,z}];
	} else {
		return shapeCache[{x,y,z}];
	}
}
