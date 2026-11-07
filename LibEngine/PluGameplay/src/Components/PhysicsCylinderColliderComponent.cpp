//
// Created by Plutex on 2026-03-09.
//

#include "PluEngine/Gameplay/Components/PhysicsCylinderColliderComponent.h"

#include "Jolt/Physics/Collision/Shape/CylinderShape.h"
#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/PluUtils.h"

Plu::PhysicsCylinderColliderComponent::PhysicsCylinderColliderComponent()
{
	HalfHeight = 1.0f;
	Radius = 1.0f;
}

void Plu::PhysicsCylinderColliderComponent::SetHalfHeight(float newHalfHeight)
{
	HalfHeight = newHalfHeight;
	DispatchEvent("ShapeChanged", nullptr);
}

void Plu::PhysicsCylinderColliderComponent::SetRadius(float newRadius)
{
	Radius = newRadius;
	DispatchEvent("ShapeChanged", nullptr);
}

JPH::ShapeRefC Plu::PhysicsCylinderColliderComponent::GetShape()
{
	float x = Plu::ClampF(Radius, 0.001f, FLT_MAX);
	float y = Plu::ClampF(HalfHeight, 0.001f, FLT_MAX);

	static HashMap<Vec2, JPH::ShapeRefC> shapeCache;
	if (!shapeCache.Contains({x,y})) {
		shapeCache[{x,y}] = new JPH::CylinderShape(y,x);
		return shapeCache[{x,y}];
	} else {
		return shapeCache[{x,y}];
	}
}
