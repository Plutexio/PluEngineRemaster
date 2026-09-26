//
// Created by Plutex on 2026-09-15.
//

#ifndef PLUENGINE_RENDERPARTICLESTATS_H
#define PLUENGINE_RENDERPARTICLESTATS_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/Objects/EngineObjectHandle.h"
#include "PluEngine/Effects/Particles/ParticleSpawner.h"
#include "PluEngine/Effects/Particles/CompiledParticleSystem.h"
#include "String/String.h"

namespace Plu
{
    // One op of an emitter program with its time on the last tick.
    struct ParticleOpDebugRow
    {
        String Name;   // "update 3 MulCurve -> col 13"
        float Ms = 0.0f;
    };

    // One emitter of an asset-driven spawner, as the render thread holds it.
    struct ParticleEmitterDebugStats
    {
        String Name;
        bool Enabled = true;
        UInt32 AliveParticles = 0;
        UInt32 MaxParticles = 0;
        float Time = 0.0f;             // emission clock
        bool EmissionDone = false;
        float SpawnRate = 0.0f;
        UInt32 BurstCount = 0;

        bool HasSprite = false;
        bool HasRibbon = false;
        EParticleRibbonMode RibbonMode = EParticleRibbonMode::PerEmitter;
        bool OrderedStorage = false;
        UInt32 HistorySamples = 0;     // per-particle ribbon trail length

        UInt32 SpawnOpCount = 0;
        UInt32 UpdateOpCount = 0;
        UInt32 UsedColumns = 0;        // SoA columns allocated
        UInt64 CpuBytes = 0;           // column + history storage
        UInt64 GpuBytes = 0;           // point / instance / history buffers

        bool HasBounds = false;
        Vec3 BoundsMin = Vec3(0.0f);
        Vec3 BoundsMax = Vec3(0.0f);

        // Last tick's per-op times. Filled only while RequestParticleOpTimings() is renewed — timing an op
        // costs a clock read per op per block.
        DynamicArray<ParticleOpDebugRow> Ops;
    };

    // A spawner driven by a ParticleSystem asset (the legacy ones are ParticleSpawnerDebugStats).
    struct ParticleSystemSpawnerDebugStats
    {
        UInt64 UUID = 0;
        UInt64 SystemUuid = 0;
        UInt32 Revision = 0;           // CompileRevision of the adopted program
        Vec3 Location = Vec3(0.0f);

        // Lifecycle as the render thread last synced it (compare with the component on main).
        EParticleEmissionState State = EParticleEmissionState::Stopped;
        UInt32 ActivationVersion = 0;
        UInt32 ClearVersion = 0;
        UInt32 CompletedActivationVersion = 0;
        UInt64 SyncedRequestedParticles = 0;

        // Parameter block the emitters ran with: the snapshot's values, or the asset defaults when the
        // block was missing or did not match the program (UsingDefaults).
        DynamicArray<CompiledParameterSlot> ParameterLayout;
        DynamicArray<float> ParameterValues;
        bool UsingDefaults = false;

        DynamicArray<ParticleEmitterDebugStats> Emitters;
    };
    // Particle state as the render thread saw it at the end of one particle tick (Debug Particles
    // panel). Particles live on the render thread and main never touches a ParticleSpawner
    // (MULTITHREADING.md), so this is a copy published under a mutex rather than a live view.
    struct ParticleDebugStats
    {
        // Bumped on every publish; 0 = nothing published yet. Lets the reader tell stale data apart.
        UInt64 PublishCount = 0;

        // World whose snapshot was rendered, and the particle delta time used for it.
        EngineObjectHandle SceneHandle;
        float DeltaTime = 0.0f;

        // Every legacy spawner of SceneHandle's world.
        DynamicArray<ParticleSpawnerDebugStats> Spawners;
        // Every asset-driven spawner of SceneHandle's world.
        DynamicArray<ParticleSystemSpawnerDebugStats> SystemSpawners;

        // Spawners the renderer still holds for other worlds (e.g. the PIE world after PIE ends,
        // which is only reconciled when it publishes again).
        UInt32 OtherWorldSpawners = 0;
        UInt64 OtherWorldAliveParticles = 0;
    };

    // Gathering walks every particle, so it only happens on request: the reader calls
    // RequestParticleDebugStats every frame it wants data, the render thread consumes the request
    // once per rendered snapshot and publishes. Same renew-every-frame contract as
    // RenderingManager::RequestShadowCascadeView. All four are thread-safe.
    PLURENDER_API void RequestParticleDebugStats();
    // Render thread. Returns true (and clears the request) if somebody asked since the last call.
    PLURENDER_API bool ConsumeParticleDebugStatsRequest();
    // Render thread. Stamps PublishCount.
    PLURENDER_API void PublishParticleDebugStats(ParticleDebugStats&& stats);
    // Copy of the last published stats.
    PLURENDER_API ParticleDebugStats GetParticleDebugStats();

    // Per-op timing of asset-driven emitters (ParticleEmitterDebugStats::Ops, also recorded to the Profiler
    // as "Particles/<emitter>/<stage> <i> <Op>"). Same renew-every-frame contract as the stats request: the
    // render thread consumes it once per tick and times that tick only. Thread-safe.
    PLURENDER_API void RequestParticleOpTimings();
    PLURENDER_API bool ConsumeParticleOpTimingsRequest();
}

#endif //PLUENGINE_RENDERPARTICLESTATS_H
