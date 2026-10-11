//
// Created by Plutex on 10/11/26.
//

#include "PluEngine/Gameplay/Components/SpringArmComponent.h"

#include "PluEngine/Gameplay/Controller.h"
#include "PluEngine/Gameplay/Puppet.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"

void Plu::SpringArmComponent::OnUpdate(float deltaTime)
{
    Vec3 direction;
    TUsePointer<Puppet> parent = DynamicCast<Puppet>(GetParentGameObject());
    if (UsePuppetControlRotation && parent) {
        direction = parent->GetController()->GetControlRotationForPuppet();
    } else {
        direction = GetWorldRotation();
    }

    RaycastHitInfo result = GetWorld()->ShootRaycastInDirection(GetWorldLocation(), direction, ArmLength, {GetParentGameObject().GetRaw()});
    GetWorld()->GetDebugDraw()->DrawDebugRaycast(result);
    if (result.Hit) {
        GetWorld()->GetDebugDraw()->DrawDebugSphere(result.HitLocation, 0.2, Vec3(0,1,0));
    } else {
        GetWorld()->GetDebugDraw()->DrawDebugSphere(GetWorldLocation() + (direction * ArmLength), 0.2, Vec3(1,0,0));
    }
}
