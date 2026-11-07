//
// Created by Plutex on 2026-03-09.
//

#include "PluEngine/Gameplay/Components/PhysicsSphereColliderComponent.h"

#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/PluUtils.h"

Plu::PhysicsSphereColliderComponent::PhysicsSphereColliderComponent()
{
	SphereRadius = 1;
}

void Plu::PhysicsSphereColliderComponent::SetSphereRadius(float newRadius)
{
	SphereRadius = newRadius;
	DispatchEvent("ShapeChanged", nullptr);
}

JPH::ShapeRefC Plu::PhysicsSphereColliderComponent::GetShape()
{
	float x = Plu::ClampF(SphereRadius, 0.001f, FLT_MAX);

	static HashMap<float, JPH::ShapeRefC> shapeCache;
	if (!shapeCache.Contains(x)) {
		shapeCache[x] = new JPH::SphereShape(x);
		return shapeCache[x];
	} else {
		return shapeCache[x];
	}
}
