//
// Created by Plutex on 2026-03-09.
//

#ifndef PLUENGINE_PHYSICSBOXCOMPONENT_H
#define PLUENGINE_PHYSICSBOXCOMPONENT_H
#include "PhysicsColliderComponent.h"
#include "PluEngine/Core.h"
#include "PhysicsBoxColliderComponent.generated.h"

namespace Plu
{
	PLU_CLASS(PyExport)
	class PLUGAMEPLAY_API PhysicsBoxColliderComponent : public PhysicsColliderComponent
	{
		REFLECTION_BODY_PHYSICSBOXCOLLIDERCOMPONENT()
	public:
		PhysicsBoxColliderComponent();
		virtual ~PhysicsBoxColliderComponent() override = default;

		PLU_PROPERTY(PyExport, Setter=SetBoxSize)
		Vec3 BoxSize;

		PLU_FUNCTION()
		void SetBoxSize(Vec3 newBoxSize);

		JPH::ShapeRefC GetShape() override;
	};
}

#endif //PLUENGINE_PHYSICSBOXCOMPONENT_H