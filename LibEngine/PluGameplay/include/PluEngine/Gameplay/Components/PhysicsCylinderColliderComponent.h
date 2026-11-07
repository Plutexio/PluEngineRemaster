//
// Created by Plutex on 9/11/26.
//

#ifndef PLUENGINE_PHYSICSCYLINDERCOLLIDERCOMPONENT_H
#define PLUENGINE_PHYSICSCYLINDERCOLLIDERCOMPONENT_H

#include "PhysicsColliderComponent.h"
#include "PluEngine/Core.h"
#include "PhysicsCylinderColliderComponent.generated.h"

namespace Plu
{
    PLU_CLASS(PyExport)
    class PLUGAMEPLAY_API PhysicsCylinderColliderComponent : public PhysicsColliderComponent
    {
        REFLECTION_BODY_PHYSICSCYLINDERCOLLIDERCOMPONENT()
    public:
        PhysicsCylinderColliderComponent();
        virtual ~PhysicsCylinderColliderComponent() override = default;

        PLU_PROPERTY(PyExport, Setter=SetHalfHeight)
        float HalfHeight = 1.0f;

        PLU_PROPERTY(PyExport, Setter=SetRadius)
        float Radius = 1.0f;

        PLU_FUNCTION()
        void SetHalfHeight(float newHalfHeight);
        PLU_FUNCTION()
        void SetRadius(float newRadius);

        JPH::ShapeRefC GetShape() override;
    };
}

#endif //PLUENGINE_PHYSICSCYLINDERCOLLIDERCOMPONENT_H
