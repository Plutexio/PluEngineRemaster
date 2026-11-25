//
// Created by Plutex on 9/11/26.
//

#ifndef PLUENGINE_PHYSICSSPHERECOLLIDERCOMPONENT_H
#define PLUENGINE_PHYSICSSPHERECOLLIDERCOMPONENT_H

#include "PhysicsColliderComponent.h"
#include "PluEngine/Core.h"
#include "PhysicsSphereColliderComponent.generated.h"

namespace Plu
{
    PLU_CLASS(PyExport)
    class PLUGAMEPLAY_API PhysicsSphereColliderComponent : public PhysicsColliderComponent
    {
        REFLECTION_BODY_PHYSICSSPHERECOLLIDERCOMPONENT()
    public:
        PhysicsSphereColliderComponent();
        virtual ~PhysicsSphereColliderComponent() override = default;

        PLU_PROPERTY(PyExport, Setter=SetSphereRadius)
        float SphereRadius;

        PLU_FUNCTION()
        void SetSphereRadius(float newRadius);

        JPH::ShapeRefC GetShape() override;
    };
}

#endif //PLUENGINE_PHYSICSSPHERECOLLIDERCOMPONENT_H
