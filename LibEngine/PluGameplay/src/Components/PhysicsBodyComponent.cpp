//
// Created by Plutex on 9/4/26.
//

#include "PluEngine/Gameplay/Components/PhysicsBodyComponent.h"

#include "PluEngine/Gameplay/Physics/PhysicsChannels.h"

void Plu::PhysicsBodyComponent::SetBodyType(PhysicsBodyType newType)
{
    Type = newType;
    PhysicsBodyType bodyType =  newType;
    DispatchEvent("SetBodyType", &bodyType);
}

void Plu::PhysicsBodyComponent::SetCollisionChannel(TUsePointer<PhysicsCollisionChannel> newChannel)
{
    CollisionChannel = newChannel;
    DispatchEvent("SetCollisionChannel", &newChannel);
}

Vec3 Plu::PhysicsBodyComponent::GetLinearVelocity() const
{
    Vec3 linearVelocity = LinearVelocity;
    DispatchEvent("GetLinearVelocity", &linearVelocity);
    return linearVelocity;
}

void Plu::PhysicsBodyComponent::SetLinearVelocity(const Vec3 &Velocity) const
{
    Vec3 linearVelocity = Velocity;
    DispatchEvent("SetLinearVelocity", &linearVelocity);
}

void Plu::PhysicsBodyComponent::AddLinearVelocity(const Vec3 &Velocity) const
{
    Vec3 linearVelocity = Velocity;
    DispatchEvent("AddLinearVelocity", &linearVelocity);
}

Vec3 Plu::PhysicsBodyComponent::GetAngularVelocity() const
{
    Vec3 angularVelocity = AngularVelocity;
    DispatchEvent("GetAngularVelocity", &angularVelocity);
    return angularVelocity;
}

void Plu::PhysicsBodyComponent::SetAngularVelocity(const Vec3 &AngularVelocity) const
{
    Vec3 angularVelocity = AngularVelocity;
    DispatchEvent("SetAngularVelocity", &angularVelocity);
}

float Plu::PhysicsBodyComponent::GetFriction() const
{
    float friction = Friction;
    DispatchEvent("GetFriction", &friction);
    return friction;
}

void Plu::PhysicsBodyComponent::SetFriction(float Friction) const
{
    DispatchEvent("SetFriction", &Friction);
}

float Plu::PhysicsBodyComponent::GetRestitution() const
{
    float restitution = Restitution;
    DispatchEvent("GetRestitution", &restitution);
    return restitution;
}

void Plu::PhysicsBodyComponent::SetRestitution(float Restitution) const
{
    DispatchEvent("SetRestitution", &Restitution);
}

void Plu::PhysicsBodyComponent::AddForce(const Vec3 &Force) const
{
    Vec3 force = Force;
    DispatchEvent("AddForce", &force);
}

void Plu::PhysicsBodyComponent::AddTorque(const Vec3 &Torque) const
{
    Vec3 torque = Torque;
    DispatchEvent("AddTorque", &torque);
}

void Plu::PhysicsBodyComponent::AddImpulse(const Vec3 &Impulse) const
{
    Vec3 impulse = Impulse;
    DispatchEvent("AddImpulse", &impulse);
}

void Plu::PhysicsBodyComponent::AddAngularImpulse(const Vec3 &Impulse) const
{
    Vec3 angularImpulse = Impulse;
    DispatchEvent("AddAngularImpulse", &angularImpulse);
}
