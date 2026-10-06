//
// Created by Plutex on 2026-03-07.
//

#include "PluEngine/Physics/PhysicsBody.h"
#include <Jolt/Physics/Body/Body.h>
#include "PluEngine/Log.h"

using namespace Plu;

PhysicsBody::PhysicsBody(
    JPH::BodyInterface& BodyInterface,
    JPH::ShapeRefC      Shape,
    const JPH::RVec3&   Position,
    const JPH::Quat&    Rotation,
    PhysicsBodyType            Type,
    TUsePointer<PhysicsCollisionChannel> Channel,
    UInt64 UserData,
    float               Friction,
    float               Restitution,
    float               Mass,
    bool                DeferAdd)
    : mBodyInterface(BodyInterface)
{
    PLU_CORE_ASSERT(Channel, "Channel cannot be null!");

    JPH::BodyCreationSettings Settings(
        Shape,
        Position,
        Rotation,
        ToJoltMotionType(Type),
        ToJoltLayer(Channel.GetRaw(),Type)
    );

    Settings.mAllowedDOFs = JPH::EAllowedDOFs::All;
    // Overlap (sensor) vs block is decided per-pair from the collision profile in the contact
    // listener — bodies are never whole-body sensors. A "Trigger" is just a preset with Overlap
    // responses (see CollisionChannels).
    Settings.mFriction = Friction;
    Settings.mRestitution = Restitution;

    JPH::MassProperties massProperties;
    massProperties.mMass = Mass;
    Settings.mMassPropertiesOverride = massProperties;
    Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;

    double x = 0.0f;
    int y = static_cast<int>(x);

    Settings.mUserData = UserData;

    // UE-style channels: the profile index is read back by the contact listener via
    // CollisionGroup::GetGroupID(). No group filter is attached (see PhysicsCollisionRules.h).
    //Settings.mCollisionGroup = JPH::CollisionGroup(nullptr, CollisionProfileIndex, 0); TODO

    mNeedsActivation = Type != PhysicsBodyType::Static;

    if (DeferAdd) {
        // Created but not inserted — the caller adds this in a batch. Until then the body exists
        // (it has an ID) but is invisible to simulation and queries.
        JPH::Body* body = mBodyInterface.CreateBody(Settings);
        mBodyID = body ? body->GetID() : JPH::BodyID();
    } else {
        mBodyID = mBodyInterface.CreateAndAddBody(
            Settings,
            mNeedsActivation
                ? JPH::EActivation::Activate
                : JPH::EActivation::DontActivate
        );
    }

    mBodyType = Type;

    // Jolt hands back no body once PhysicsWorld's kMaxBodies is reached.
    if (mBodyID.IsInvalid()) {
        PLU_CORE_ERROR("Failed to create physics body — the physics system is out of bodies");
    }
}

void PhysicsBody::SetCollisionChannel(PhysicsCollisionChannel *Channel) const
{
    mBodyInterface.SetObjectLayer(mBodyID, ToJoltLayer(Channel, mBodyType));
}

PhysicsBody::~PhysicsBody() {
    if (IsValid()) {
        // A body created with DeferAdd that was destroyed before its batch went in was never added,
        // and RemoveBody on a body that is not in the system is an error.
        if (mBodyInterface.IsAdded(mBodyID)) {
            mBodyInterface.RemoveBody(mBodyID);
        }
        mBodyInterface.DestroyBody(mBodyID);
    }
}

JPH::RVec3 PhysicsBody::GetPosition() const {
    return mBodyInterface.GetPosition(mBodyID);
}

JPH::Quat PhysicsBody::GetRotation() const {
    return mBodyInterface.GetRotation(mBodyID);
}

void PhysicsBody::SetPosition(const JPH::RVec3& Position) {
    mBodyInterface.SetPosition(mBodyID, Position, JPH::EActivation::Activate);
}

void PhysicsBody::SetRotation(const JPH::Quat& Rotation) {
    mBodyInterface.SetRotation(mBodyID, Rotation, JPH::EActivation::Activate);
}

JPH::Vec3 PhysicsBody::GetLinearVelocity() const {
    return mBodyInterface.GetLinearVelocity(mBodyID);
}

void PhysicsBody::SetLinearVelocity(const JPH::Vec3& Velocity) {
    mBodyInterface.SetLinearVelocity(mBodyID, Velocity);
}

void PhysicsBody::AddLinearVelocity(const JPH::Vec3& Velocity) {
    mBodyInterface.AddLinearVelocity(mBodyID, Velocity);
}

JPH::Vec3 PhysicsBody::GetAngularVelocity() const {
    return mBodyInterface.GetAngularVelocity(mBodyID);
}

void PhysicsBody::SetAngularVelocity(const JPH::Vec3& AngularVelocity) {
    mBodyInterface.SetAngularVelocity(mBodyID, AngularVelocity);
}

float PhysicsBody::GetFriction() const {
    return mBodyInterface.GetFriction(mBodyID);
}

void PhysicsBody::SetFriction(float Friction) {
    mBodyInterface.SetFriction(mBodyID, Friction);
}

float PhysicsBody::GetRestitution() const {
    return mBodyInterface.GetRestitution(mBodyID);
}

void PhysicsBody::SetRestitution(float Restitution) {
    mBodyInterface.SetRestitution(mBodyID, Restitution);
}

void PhysicsBody::AddForce(const JPH::Vec3& Force) {
    mBodyInterface.AddForce(mBodyID, Force);
}

void PhysicsBody::AddTorque(const JPH::Vec3& Torque) {
    mBodyInterface.AddTorque(mBodyID, Torque);
}

void PhysicsBody::AddImpulse(const JPH::Vec3& Impulse) {
    mBodyInterface.AddImpulse(mBodyID, Impulse);
}

void PhysicsBody::AddAngularImpulse(const JPH::Vec3& Impulse) {
    mBodyInterface.AddAngularImpulse(mBodyID, Impulse);
}

JPH::EMotionType PhysicsBody::ToJoltMotionType(PhysicsBodyType Type) {
    switch (Type) {
        case PhysicsBodyType::Dynamic:   return JPH::EMotionType::Dynamic;
        case PhysicsBodyType::Kinematic: return JPH::EMotionType::Kinematic;
        default:                   return JPH::EMotionType::Static;
    }
}

JPH::ObjectLayer PhysicsBody::ToJoltLayer(PhysicsCollisionChannel *channel, PhysicsBodyType Type)
{
    UInt16 layer = PhysicsChannelsManager::GetInstance()->GetChannelId(channel);
    layer = layer << 1 | (Type != PhysicsBodyType::Static);
    return layer;
}
