//
// Created by Plutex on 8/31/26.
//

#ifndef PLUENGINE_PARTICLESPAWNER_H
#define PLUENGINE_PARTICLESPAWNER_H

#include "PluEngine/Core.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "PluEngine/Effects/Particles/Particle.h"
#include "ParticleSpawner.generated.h"
#include "PluBase/PluUUID.h"

namespace Plu
{
    struct RenderSnapshot;

    // Debug snapshot of one spawner (Debug Particles panel). Plain values only, so it can be copied
    // off the render thread; see ParticleSpawner::GatherDebugStats.
    struct ParticleSpawnerDebugStats
    {
        UInt64 UUID = 0;

        UInt32 AliveParticles = 0;
        // Slots allocated in the particle arrays (alive + free). Only grows.
        UInt32 PoolSize = 0;
        UInt32 FreeSlots = 0;

        // Component request counter as far as this spawner has consumed it.
        UInt64 SyncedRequestedParticles = 0;
        int LastBurstSize = 0;
        // Seconds into the current loop period.
        float LoopTime = 0.0f;
        float LoopLength = 0.0f;
        bool Loop = false;

        Vec3 Location = Vec3(0.0f);
        Vec3 LaunchDirection = Vec3(0.0f, 0.0f, -1.0f);

        // Over alive particles only; meaningless when AliveParticles == 0.
        Vec3 BoundsMin = Vec3(0.0f);
        Vec3 BoundsMax = Vec3(0.0f);
        float AverageSpeed = 0.0f;
        float MaxSpeed = 0.0f;
        float MinLifetimeLeft = 0.0f;
        float MaxLifetimeLeft = 0.0f;
    };

    PLU_CLASS()
    class PLUEFFECTS_API ParticleSpawner : public EngineObject
    {
        REFLECTION_BODY_PARTICLESPAWNER()
    private:
        // Particles as a structure of arrays, kept dense: [0, mAliveCount) are alive, everything past
        // it is allocated but free. A dying particle is replaced by the last alive one (swap-remove),
        // so there are no holes, no alive flags and no free list, and the tick is one straight loop.
        // The arrays only grow (geometrically), so a looping spawner stops allocating after warm-up.
        //
        // Positions are interleaved xyz, 3 floats per particle — the exact vertex layout the renderer
        // uploads, so they go to the GPU as they are (GetPositions).
        DynamicArray<float> mPositions;
        DynamicArray<float> mVelocitiesX;
        DynamicArray<float> mVelocitiesY;
        DynamicArray<float> mVelocitiesZ;
        // Per-particle drag coefficient (class Drag with DragRandomness applied).
        DynamicArray<float> mDrags;
        // Seconds left to live.
        DynamicArray<float> mLifetimes;
        // 1 once the particle has been faster than KillWhenSlowSpeed; KillWhenSlow waits for it.
        DynamicArray<UInt8> mSlowKillArmed;
        UInt32 mAliveCount = 0;

        PluRandom::FastRandom mRandom;
        ParticleClass mParticleClass;
        float mLoopTime = 0.0f;
        // Size of the last burst, repeated by Loop. 0 = nothing spawned yet, so no looping.
        int mLastParticleSpawned = 0;
        // Component's cumulative requested particle count already turned into particles.
        UInt64 mSyncedRequestedParticles = 0;

        Vec3 mLocation = Vec3(0.0f);
        Vec3 mLaunchDirection = Vec3(0.0f, 0.0f, -1.0f);

        // Grows every particle array to hold at least particleCount particles.
        void EnsureCapacity(UInt32 particleCount);
        void SpawnParticles(int numParticles);
        // Swap-removes every particle whose lifetime ran out (the tick zeroes it for slow kills too).
        void RemoveDeadParticles();
    public:
        ParticleSpawner() = default;
        virtual ~ParticleSpawner() override = default;

        PluUUID UUID;

        // Particles are simulated in world space: moving the spawner only moves where new particles
        // start and which way they launch, particles already in flight keep their trajectory.
        void SetParticleClass(const ParticleClass& particleClass);
        [[nodiscard]] const ParticleClass& GetParticleClass() const { return mParticleClass; }
        void SetTransform(const Vec3& location, const Vec3& launchDirection);

        // Catches up with the owning component's cumulative request counter: spawns
        // (requestedParticles - already synced) particles. Idempotent — syncing the same value again
        // spawns nothing, so a snapshot that is rendered twice or skipped cannot double or lose bursts.
        // lastBurstSize is the size Loop repeats.
        void SyncSpawnRequests(UInt64 requestedParticles, int lastBurstSize);

        void TickParticles(float deltaTime);

        // Alive particles after the last tick: interleaved xyz, GetAliveCount() * 3 floats. The
        // order is not stable (swap-remove) and the pointer is invalidated by the next sync or tick.
        [[nodiscard]] const float* GetPositions() const { return mPositions.Data(); }
        [[nodiscard]] UInt32 GetAliveCount() const { return mAliveCount; }

        // O(alive particles) walk — call only while somebody is looking.
        [[nodiscard]] ParticleSpawnerDebugStats GatherDebugStats() const;
    };
}

#endif //PLUENGINE_PARTICLESPAWNER_H
