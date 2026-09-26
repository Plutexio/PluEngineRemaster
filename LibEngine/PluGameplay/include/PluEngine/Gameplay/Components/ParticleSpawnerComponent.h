//
// Created by Plutex on 8/31/26.
//

#ifndef PLUENGINE_PARTICLESPAWNERCOMPONENT_H
#define PLUENGINE_PARTICLESPAWNERCOMPONENT_H
#include "PluEngine/Effects/Particles/Particle.h"
#include "PluEngine/Effects/Particles/ParticleAttributes.h"
#include "PluEngine/Effects/Particles/ParticleSystem.h"
#include "PluEngine/Effects/Particles/ParticleSystemInstance.h"
#include "PluEngine/Core.h"
#include "PluEngine/Gameplay/WorldComponent.h"
#include "ParticleSpawnerComponent.generated.h"

namespace Plu
{
    // Gameplay-side handle of a particle spawner. The simulation runs on the render thread
    // (MULTITHREADING.md); this component only holds the settings, the request counter and the lifecycle
    // state that RenderSnapshotBuilder packs into every snapshot.
    //
    // Two modes: with a ParticleSystemAsset the spawner runs that asset's emitters (lifecycle below,
    // parameters through GetSystemInstance()); without one it is the legacy single-class point spawner
    // driven by SpawnerParticleClass, unchanged.
    PLU_CLASS(PyExport)
    class PLUGAMEPLAY_API ParticleSpawnerComponent : public WorldComponent
    {
        REFLECTION_BODY_PARTICLESPAWNERCOMPONENT()
    private:
        // Cumulative, never reset: the render thread spawns the difference to the value it saw last.
        UInt64 mRequestedParticles = 0;
        int mLastBurstSize = 0;

        // Lifecycle, all STATE (never impulses) so it survives a dropped or replayed snapshot.
        EParticleEmissionState mEmissionState = EParticleEmissionState::Stopped;
        UInt32 mActivationVersion = 0; // ++ on Play(): restart
        UInt32 mClearVersion = 0;      // ++ on Stop(): clear now

        // Render -> main mirror, refreshed by SceneWorld::UpdateParticleLiveness once per frame.
        // mSeenByRenderer is a latch: "no liveness entry" means "not created yet", never "finished".
        bool mSeenByRenderer = false;
        UInt32 mAliveParticlesMirror = 0;
        UInt32 mCompletedActivationMirror = 0;

        TOwningPointer<ParticleSystemInstance> mSystemInstance;
    public:
        ParticleSpawnerComponent() = default;
        virtual ~ParticleSpawnerComponent() override = default;

        PLU_PROPERTY(PyExport)
        int NumParticlesToSpawn = 10;

        PLU_PROPERTY(PyExport)
        ParticleClass SpawnerParticleClass;

        // Requests a burst. Several calls in one frame add up; Loop repeats the latest burst size.
        PLU_FUNCTION(PyExport)
        void SpawnParticles(int numParticles);

        // The VFX asset this spawner plays. Null = legacy point spawner (SpawnerParticleClass).
        PLU_PROPERTY(PyExport)
        TUsePointer<ParticleSystem> ParticleSystemAsset;

        // Start playing on begin play. With no asset the legacy burst (NumParticlesToSpawn) fires instead.
        PLU_PROPERTY(PyExport)
        bool AutoActivate = true;
        // Delete the owning GameObject once the run has nothing left to emit or show (one-shot effects).
        PLU_PROPERTY(PyExport)
        bool AutoDestroyWhenFinished = false;

        // (Re)starts the effect: emitters reset and the previous run's particles are dropped.
        PLU_FUNCTION(PyExport)
        void Play();
        // Soft stop: no new particles, live ones finish.
        PLU_FUNCTION(PyExport)
        void Deactivate();
        // Hard stop: no new particles and live ones vanish now.
        PLU_FUNCTION(PyExport)
        void Stop();
        // Freezes the simulation in place; Resume continues without restarting.
        PLU_FUNCTION(PyExport)
        void Pause();
        PLU_FUNCTION(PyExport)
        void Resume();
        PLU_FUNCTION(PyExport)
        bool IsPlaying() const { return mEmissionState == EParticleEmissionState::Playing; }
        // The current run has nothing left to emit or show. False until the render thread has seen the
        // spawner at all.
        PLU_FUNCTION(PyExport)
        bool IsFinished() const;
        // Live particles across all emitters, as of the last frame the render thread reported.
        PLU_FUNCTION(PyExport)
        int GetAliveParticles() const { return static_cast<int>(mAliveParticlesMirror); }

        // Creates this component's parameter instance on first use and (re)binds it to the current asset —
        // cheap when nothing changed. Null when no asset is assigned.
        ParticleSystemInstance* EnsureSystemInstance();
        // Python: `comp.GetSystemInstance().SetVec3("TracerColor", Vec3(1, 0, 0))`.
        PLU_FUNCTION(PyExport)
        ParticleSystemInstance* GetSystemInstance();

        // SceneWorld::UpdateParticleLiveness, once per frame: latches "seen" and mirrors the counters.
        void ApplyLiveness(UInt32 aliveParticles, UInt32 completedActivationVersion)
        {
            mSeenByRenderer = true;
            mAliveParticlesMirror = aliveParticles;
            if (completedActivationVersion > mCompletedActivationMirror) mCompletedActivationMirror = completedActivationVersion;
        }

        [[nodiscard]] EParticleEmissionState GetEmissionState() const { return mEmissionState; }
        [[nodiscard]] UInt32 GetActivationVersion() const { return mActivationVersion; }
        [[nodiscard]] UInt32 GetClearVersion() const { return mClearVersion; }
        [[nodiscard]] bool WasSeenByRenderer() const { return mSeenByRenderer; }
        [[nodiscard]] UInt32 GetCompletedActivationMirror() const { return mCompletedActivationMirror; }

        // Axis of the launch cone in world space — the component's forward vector (-Z at zero rotation).
        Vec3 GetLaunchDirection();

        UInt64 GetRequestedParticles() const { return mRequestedParticles; }
        int GetLastBurstSize() const { return mLastBurstSize; }

        void OnBeginPlay() override;
        void OnUpdate(float deltaTime) override;
    };
}

#endif //PLUENGINE_PARTICLESPAWNERCOMPONENT_H
