//
// Created by Plutex on 10/11/26.
//

#include "PluEngine/Gameplay/Components/SpringArmComponent.h"

#include "PluEngine/Gameplay/Controller.h"
#include "PluEngine/Gameplay/Puppet.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"

bool Plu::SpringArmComponent::HasAttachPoints()
{
    return true;
}

DynamicArray<Plu::String> Plu::SpringArmComponent::GetAttachPoints()
{
    return {"EndOfArm"};
}

bool Plu::SpringArmComponent::GetWorldMatrixForAttachPoint(String attachPoint, Matrix4 *outMatrix)
{
    if (attachPoint == "EndOfArm") {
        Vec3 direction;
        TUsePointer<Puppet> parent = DynamicCast<Puppet>(GetParentGameObject());
        if (UsePuppetControlRotation && parent && parent->GetController()) {
            direction = parent->GetController()->GetControlRotationForPuppet();
        } else {
            direction = GetWorldForwardVector();
        }

        RaycastHitInfo result = GetWorld()->ShootRaycastInDirection(GetWorldLocation(), direction, ArmLength, {GetParentGameObject().GetRaw()});
        if (DebugDraw) {
            GetWorld()->GetDebugDraw()->DrawDebugRaycast(result);
            if (result.Hit) {
                GetWorld()->GetDebugDraw()->DrawDebugSphere(result.HitLocation, 0.2, Vec3(0,1,0));
            } else {
                GetWorld()->GetDebugDraw()->DrawDebugSphere(result.TraceEnd, 0.2, Vec3(1,0,0));
            }
        }
        Matrix4 out = glm::translate(glm::mat4(1.0f), direction * (result.HitFraction * ArmLength));
        *outMatrix = out * GetWorldMatrix();
        return true;
    } else {
        return WorldComponent::GetWorldMatrixForAttachPoint(attachPoint, outMatrix);
    }
}

void Plu::SpringArmComponent::OnUpdate(float deltaTime)
{
    for (const auto& child : GetChildren()) {
        child->SetRelativeLocation(child->GetRelativeLocation());
    }
    for (auto attachedObject : GetAttachedObjects()) {
        attachedObject->MarkWorldMatrixForRegeneration();
    }
}
