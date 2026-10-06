//
// Created by Plutex on 10/4/26.
//

#include "PluEngine/Physics/PhysicsCollisionRules.h"

#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/Collision/Shape/CompoundShape.h"
#include "PluEngine/Physics/PhysicsWorld.h"

static UInt32 GetUserData(const JPH::Body& body, const JPH::SubShapeID& subShapeId)
{
    // Jolt is built without RTTI, so dynamic_cast on its shapes does not link - check the type tag instead.
    const JPH::Shape *shape = body.GetShape();
    // StaticCompoundShapeSettings::Create collapses a single sub-shape into the shape itself (or a
    // RotatedTranslatedShape), so a body that is not a compound has exactly one collider: index 0.
    if (shape->GetType() != JPH::EShapeType::Compound) return 0;
    const JPH::CompoundShape *compound = static_cast<const JPH::CompoundShape *>(shape);
    JPH::SubShapeID remainder;
    return compound->GetSubShape(compound->GetSubShapeIndexFromID(subShapeId, remainder)).mUserData;
}

static std::pair<UInt64, UInt64> MakeContactKey(const JPH::BodyID& body1, const JPH::SubShapeID& subShape1,
                                                const JPH::BodyID& body2, const JPH::SubShapeID& subShape2)
{
    return { (static_cast<UInt64>(body1.GetIndexAndSequenceNumber()) << 32) | body2.GetIndexAndSequenceNumber(),
             (static_cast<UInt64>(subShape1.GetValue()) << 32) | subShape2.GetValue() };
}

void Plu::PluContactListener::ContactResolvement(const JPH::Body &inBody1, const JPH::Body &inBody2,
    const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) const
{
    if (PhysicsChannelsManager::GetInstance()->CanBlock(inBody1.GetObjectLayer(), inBody2.GetObjectLayer())) {
        //Block
        return;
    }
    //Overlap
    ioSettings.mIsSensor = true;

    PhysicsOverlapEventInfo overlapInfo;
    overlapInfo.End = false;

    overlapInfo.ContactKey = MakeContactKey(inBody1.GetID(), inManifold.mSubShapeID1, inBody2.GetID(), inManifold.mSubShapeID2);

    overlapInfo.ObjectA = inBody1.GetUserData();
    overlapInfo.ObjectB = inBody2.GetUserData();

    overlapInfo.ColliderA = GetUserData(inBody1, inManifold.mSubShapeID1);
    overlapInfo.ColliderB = GetUserData(inBody2, inManifold.mSubShapeID2);

    if (mPhysicsWorld->mCurrentOverlaps.Insert(overlapInfo.ContactKey, overlapInfo)) {
        mPhysicsWorld->mOverlapQueue.PushBack(overlapInfo);
    }
}

void Plu::PluContactListener::OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings)
{
    ContactResolvement(inBody1, inBody2, inManifold, ioSettings);
}

void Plu::PluContactListener::OnContactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2,
    const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings)
{
    if (PhysicsChannelsManager::GetInstance()->CanBlock(inBody1.GetObjectLayer(), inBody2.GetObjectLayer())) {
        //Block
        return;
    }
    //Overlap
    ioSettings.mIsSensor = true;
}

void Plu::PluContactListener::OnContactRemoved(const JPH::SubShapeIDPair &inSubShapePair)
{
    PhysicsOverlapEventInfo overlapInfo;
    overlapInfo.End = true;

    overlapInfo.ContactKey = MakeContactKey(inSubShapePair.GetBody1ID(), inSubShapePair.GetSubShapeID1(), inSubShapePair.GetBody2ID(), inSubShapePair.GetSubShapeID2());

    if (mPhysicsWorld->mCurrentOverlaps.Contains(overlapInfo.ContactKey)) {
        mPhysicsWorld->mOverlapQueue.PushBack(overlapInfo);
    }
}

void Plu::PluContactListener::SetPhysicsWorld(PhysicsWorld *physicsWorld)
{
    mPhysicsWorld = physicsWorld;
}
