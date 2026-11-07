//
// Created by Plutex on 2026-03-08.
//

#ifndef PLUENGINE_PHYSICSCOLLIDERCOMPONENT_H
#define PLUENGINE_PHYSICSCOLLIDERCOMPONENT_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "PluEngine/Core.h"
#include "PluEngine/Gameplay/WorldComponent.h"
#include "PhysicsColliderComponent.generated.h"

namespace Plu
{
	PLU_CLASS(Abstract, PyExport)
	class PLUGAMEPLAY_API PhysicsColliderComponent : public WorldComponent
	{
		REFLECTION_BODY_PHYSICSCOLLIDERCOMPONENT()
	public:
		PhysicsColliderComponent() = default;
		virtual ~PhysicsColliderComponent() override = default;

		virtual JPH::ShapeRefC GetShape() = 0;
	};
}

#endif //PLUENGINE_PHYSICSBODYCOMPONENT_H
