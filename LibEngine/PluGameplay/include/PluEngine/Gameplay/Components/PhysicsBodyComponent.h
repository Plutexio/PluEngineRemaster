//
// Created by Plutex on 9/4/26.
//

#ifndef PLUENGINE_PHYSICSBODYCOMPONENT_H
#define PLUENGINE_PHYSICSBODYCOMPONENT_H

#include "PluEngine/Core.h"
#include "PluEngine/Gameplay/GameObjectComponent.h"
#include "PhysicsBodyComponent.generated.h"

namespace Plu
{
    PLU_ENUM(PyExport, PyNamespace=Plu)
    enum class PhysicsBodyType {
        Static,
        Dynamic,
        Kinematic
    };

    PLU_CLASS(PyExport)
    class PLUGAMEPLAY_API PhysicsBodyComponent : public GameObjectComponent
    {
        REFLECTION_BODY_PHYSICSBODYCOMPONENT()
    public:
        PhysicsBodyComponent() = default;
        virtual ~PhysicsBodyComponent() override = default;

        PLU_PROPERTY(Getter=GetLinearVelocity, Setter=SetLinearVelocity, PyExport);
        Vec3 LinearVelocity = {0.0f, 0.0f, 0.0f};
        PLU_PROPERTY(Getter=GetAngularVelocity, Setter=SetAngularVelocity, PyExport);
        Vec3 AngularVelocity = {0.0f, 0.0f, 0.0f};
        PLU_PROPERTY(Getter=GetFriction, Setter=SetFriction, PyExport);
        float Friction = 1;
        PLU_PROPERTY(Getter=GetRestitution, Setter=SetRestitution, PyExport);
        float Restitution = 0;
        PLU_PROPERTY(Setter=SetBodyType, PyExport)
        PhysicsBodyType Type = PhysicsBodyType::Dynamic;

        float Mass = 10.0f;

        void SetBodyType(PhysicsBodyType newType);

        PLU_FUNCTION()
        [[nodiscard]] Vec3 GetLinearVelocity() const;
        PLU_FUNCTION()
        void SetLinearVelocity(const Vec3& Velocity) const;
        PLU_FUNCTION()
        void AddLinearVelocity(const Vec3& Velocity) const;

        PLU_FUNCTION()
        [[nodiscard]] Vec3 GetAngularVelocity() const;
        PLU_FUNCTION()
        void SetAngularVelocity(const Vec3& AngularVelocity) const;

        PLU_FUNCTION()
        [[nodiscard]] float GetFriction() const;
        PLU_FUNCTION()
        void  SetFriction(float Friction) const;

        PLU_FUNCTION()
        [[nodiscard]] float GetRestitution() const;
        PLU_FUNCTION()
        void  SetRestitution(float Restitution) const;

        PLU_FUNCTION()
        void AddForce(const Vec3& Force) const;
        PLU_FUNCTION()
        void AddTorque(const Vec3& Torque) const;
        PLU_FUNCTION()
        void AddImpulse(const Vec3& Impulse) const;
        PLU_FUNCTION()
        void AddAngularImpulse(const Vec3& Impulse) const;
    };
}

#endif //PLUENGINE_PHYSICSBODYCOMPONENT_H
