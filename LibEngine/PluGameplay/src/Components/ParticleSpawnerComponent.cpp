//
// Created by Plutex on 8/31/26.
//

#include "PluEngine/Gameplay/Components/ParticleSpawnerComponent.h"

#include "PluEngine/PluUtils.h"
#include "PluEngine/Gameplay/GameObject.h"

void Plu::ParticleSpawnerComponent::SpawnParticles(int numParticles)
{
    if (numParticles <= 0) return;
    mRequestedParticles += static_cast<UInt64>(numParticles);
    mLastBurstSize = numParticles;
}

Vec3 Plu::ParticleSpawnerComponent::GetLaunchDirection()
{
    return GetForwardVector(GetWorldRotation());
}

void Plu::ParticleSpawnerComponent::OnBeginPlay()
{
    if (ParticleSystemAsset) {
        if (AutoActivate) Play();
        return;
    }
    // Legacy point spawner: the unconditional burst, exactly as before.
    SpawnParticles(NumParticlesToSpawn);
}

void Plu::ParticleSpawnerComponent::Play()
{
    mEmissionState = EParticleEmissionState::Playing;
    ++mActivationVersion;
}

void Plu::ParticleSpawnerComponent::Deactivate()
{
    mEmissionState = EParticleEmissionState::Stopped; // no bump: live particles are left to finish
}

void Plu::ParticleSpawnerComponent::Stop()
{
    mEmissionState = EParticleEmissionState::Stopped;
    ++mClearVersion;
}

void Plu::ParticleSpawnerComponent::Pause()
{
    if (mEmissionState == EParticleEmissionState::Playing) mEmissionState = EParticleEmissionState::Paused;
}

void Plu::ParticleSpawnerComponent::Resume()
{
    // No version bump: resuming must not restart anything.
    if (mEmissionState == EParticleEmissionState::Paused) mEmissionState = EParticleEmissionState::Playing;
}

bool Plu::ParticleSpawnerComponent::IsFinished() const
{
    return mActivationVersion > 0 && mSeenByRenderer && mCompletedActivationMirror >= mActivationVersion;
}

Plu::ParticleSystemInstance* Plu::ParticleSpawnerComponent::EnsureSystemInstance()
{
    if (!ParticleSystemAsset) return nullptr;
    if (!mSystemInstance) {
        mSystemInstance = CreateOwning<ParticleSystemInstance>();
#ifdef PLU_ENGINE_EDITOR_BUILD
        TUsePointer<GameObject> owner = GetParentGameObject();
        mSystemInstance->DebugName = (owner ? owner->GetObjectName() : String("?")) + " / " + GetClass()->TypeName;
#endif
    }
    mSystemInstance->BindTo(ParticleSystemAsset);
    return mSystemInstance.GetRaw();
}

Plu::ParticleSystemInstance* Plu::ParticleSpawnerComponent::GetSystemInstance()
{
    return EnsureSystemInstance();
}

void Plu::ParticleSpawnerComponent::OnUpdate(float deltaTime)
{
}
