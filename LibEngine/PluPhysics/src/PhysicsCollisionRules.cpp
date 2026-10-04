//
// Created by Plutex on 10/4/26.
//

#include "PluEngine/Physics/PhysicsCollisionRules.h"

#include "Jolt/Physics/Body/Body.h"

void Plu::PluContactListener::OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings)
{
    if (PhysicsChannelsManager::GetInstance()->CanBlock(inBody1.GetObjectLayer(), inBody2.GetObjectLayer())) {
        //Block
        return;
    }
    //Overlap
    ioSettings.mIsSensor = true;
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

}
