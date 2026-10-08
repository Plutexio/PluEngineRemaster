//
// Created by Plutex on 6/22/26.
//

#include "PluEngine/Render/Renderer.h"
#include "PluEngine/Render/MeshDraw.h"
#include "PluEngine/Render/RenderUsageStats.h"

#include <glad/glad.h>
#include <cmath>
#include "PluEngine/Core/ApplicationInfo.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Render/RenderThreading.h"
#include "PluEngine/Render/RenderUtils.h"
#include "PluEngine/Render/ShaderProgram.h"
#include "PluEngine/Platform/Window.h"
#include "PluEngine/AssetTypes/Material/Material.h"
#include "PluEngine/Render/RenderingManager.h"
#include "PluEngine/Render/ShadersManager.h"
#include "EngineAssets.h"
#include "PluEngine/Render/GPUProfiler.h"
#include "PluEngine/Timer.h"
#include "PluEngine/AssetTypes/SkeletalMesh/SkeletalMesh.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Effects/Particles/ParticleSpawner.h"
#include "PluEngine/Render/RenderParticleLiveness.h"
#include <cstring>
#include "PluEngine/Render/RenderParticleStats.h"
#include "PluEngine/AssetTypes/Texture/Texture.h"
#include "PluEngine/Render/GLTexture.h"
#include <algorithm>

namespace
{
    // Minimum seconds between two Debug Particles publishes. Well under the panel's 0.5 s
    // "no render update" warning.
    constexpr float kParticleDebugStatsInterval = 0.1f;

    // Per-cascade GPU scope names, built once. GPUProfileScope takes a String, so building them
    // inline would mean a heap allocation per cascade per frame on the render thread.
    // Drains the GL error queue and logs anything in it against a label.
    //
    // Worth the call: the only other places that drain are ShaderStorageBuffer/UniformBuffer, so
    // an error raised anywhere on the shadow path used to surface under whichever buffer Update
    // ran next — a name that has nothing to do with the culprit. Calling this at each step of the
    // shadow setup makes the log name the actual failing operation.
    bool CheckShadowGLError(const char* where)
    {
        bool clean = true;
        GLenum err;
        while ((err = glGetError()) != GL_NO_ERROR)
        {
            clean = false;
            const char* msg;
            switch (err)
            {
                case GL_INVALID_ENUM:                  msg = "INVALID_ENUM"; break;
                case GL_INVALID_VALUE:                 msg = "INVALID_VALUE"; break;
                case GL_INVALID_OPERATION:             msg = "INVALID_OPERATION"; break;
                case GL_INVALID_FRAMEBUFFER_OPERATION: msg = "INVALID_FRAMEBUFFER_OPERATION"; break;
                case GL_OUT_OF_MEMORY:                 msg = "OUT_OF_MEMORY"; break;
                default:                               msg = "UNKNOWN"; break;
            }
            PLU_CORE_ERROR("OpenGL Error at {}: {} (0x{:x})", where, msg, err);
        }
        return clean;
    }

    const DynamicArray<Plu::String>& CascadeGpuScopeNames()
    {
        static const DynamicArray<Plu::String> names = [] {
            DynamicArray<Plu::String> result;
            result.Reserve(Plu::kMaxShadowCascades);
            for (Int32 c = 0; c < Plu::kMaxShadowCascades; c++) {
                Plu::String name = "Renderer::ShadowCascade";
                name += Plu::String::FromInt(c);
                result.PushBack(name);
            }
            return result;
        }();
        return names;
    }

    // Same trick for the spot shadow slots — built once, so the per-slot GPU scope costs no
    // allocation on the render thread.
    const DynamicArray<Plu::String>& SpotShadowGpuScopeNames()
    {
        static const DynamicArray<Plu::String> names = [] {
            DynamicArray<Plu::String> result;
            result.Reserve(Plu::kMaxSpotShadowSlots);
            for (Int32 s = 0; s < Plu::kMaxSpotShadowSlots; s++) {
                Plu::String name = "Renderer::SpotShadowSlot";
                name += Plu::String::FromInt(s);
                result.PushBack(name);
            }
            return result;
        }();
        return names;
    }
}

namespace
{
    UInt32 PackUnorm8(float value)
    {
        value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<UInt32>(value * 255.0f + 0.5f);
    }

    // Grows geometrically: an emitter whose count creeps up by a few particles every frame would
    // otherwise reallocate (and copy tens of MB) every frame.
    template <typename T>
    void ResizeScratch(DynamicArray<T>& scratch, UInt32 count)
    {
        if (count > scratch.Capacity()) scratch.Reserve(std::max<UInt32>(count, scratch.Capacity() * 2));
        scratch.Resize(count);
    }

    // SoA columns of one sprite emitter -> ParticleInstanceGPU (layout of ParticleSprite.vert). Also returns
    // the mean position of the live particles: the sprite pass sorts emitters by it.
    void PackParticleSpriteInstances(const Plu::ParticleEmitterInstance& emitter, const Matrix4& view,
                                     DynamicArray<Plu::ParticleInstanceGPU>& out, Vec3& outCentroid)
    {
        PLU_PROFILE_SCOPE("Particles/SpritePack");
        using Plu::EParticleColumn;
        const Plu::ParticleBlockStorage& storage = emitter.GetStorage();
        const UInt32 count = storage.Alive();
        ResizeScratch(out, count);
        if (count == 0) return;

        const float* px = storage.Column(EParticleColumn::PosX);
        const float* py = storage.Column(EParticleColumn::PosY);
        const float* pz = storage.Column(EParticleColumn::PosZ);
        const float* vx = storage.Column(EParticleColumn::VelX);
        const float* vy = storage.Column(EParticleColumn::VelY);
        const float* vz = storage.Column(EParticleColumn::VelZ);
        const float* sx = storage.Column(EParticleColumn::SizeX);
        const float* sy = storage.Column(EParticleColumn::SizeY);
        const float* cr = storage.Column(EParticleColumn::ColR);
        const float* cg = storage.Column(EParticleColumn::ColG);
        const float* cb = storage.Column(EParticleColumn::ColB);
        const float* ca = storage.Column(EParticleColumn::ColA);
        // On-demand columns: null when no module asked for them.
        const float* rotation = storage.Column(EParticleColumn::Rotation);
        const float* frame = storage.Column(EParticleColumn::SubUVFrame);

        // Velocity stretch runs in view space: store the on-screen part of the velocity (the first two
        // rows of the view rotation). Moving straight at the camera correctly gives no stretch.
        const bool stretched = emitter.GetProgram().Sprite.Facing == Plu::EParticleFacingMode::VelocityStretched;
        const float rx0 = view[0][0], rx1 = view[1][0], rx2 = view[2][0];
        const float ry0 = view[0][1], ry1 = view[1][1], ry2 = view[2][1];

        float sumX = 0.0f, sumY = 0.0f, sumZ = 0.0f;
        Plu::ParticleInstanceGPU* dst = out.Data();
        for (UInt32 i = 0; i < count; ++i) {
            Plu::ParticleInstanceGPU& instance = dst[i];
            instance.PositionRotation.x = px[i];
            instance.PositionRotation.y = py[i];
            instance.PositionRotation.z = pz[i];
            instance.PositionRotation.w = rotation ? rotation[i] : 0.0f;
            instance.SizeAndVelocity.x = sx[i] * 0.5f;
            instance.SizeAndVelocity.y = sy[i] * 0.5f;
            instance.SizeAndVelocity.z = stretched ? rx0 * vx[i] + rx1 * vy[i] + rx2 * vz[i] : 0.0f;
            instance.SizeAndVelocity.w = stretched ? ry0 * vx[i] + ry1 * vy[i] + ry2 * vz[i] : 0.0f;
            instance.ColorRGBA8 = PackUnorm8(cr[i]) | (PackUnorm8(cg[i]) << 8) | (PackUnorm8(cb[i]) << 16) | (PackUnorm8(ca[i]) << 24);
            instance.SubUVFrameFlags = frame ? (static_cast<UInt32>(frame[i]) & 0xFFFFu) : 0u;
            instance.Pad0 = 0;
            instance.Pad1 = 0;
            sumX += px[i]; sumY += py[i]; sumZ += pz[i];
        }
        const float inv = 1.0f / static_cast<float>(count);
        outCentroid = Vec3(sumX * inv, sumY * inv, sumZ * inv);
    }

    // SoA columns of one ribbon emitter -> ParticleInstanceGPU in the ribbon meaning (ParticleRibbon.vert):
    // w = texture u (normalised age), x = half width. Storage is ordered, so the output is in spawn order.
    void PackParticleRibbonInstances(const Plu::ParticleEmitterInstance& emitter,
                                     DynamicArray<Plu::ParticleInstanceGPU>& out, Vec3& outCentroid)
    {
        PLU_PROFILE_SCOPE("Particles/RibbonPack");
        using Plu::EParticleColumn;
        const Plu::ParticleBlockStorage& storage = emitter.GetStorage();
        const UInt32 count = storage.Alive();
        ResizeScratch(out, count);
        if (count == 0) return;

        const float* px = storage.Column(EParticleColumn::PosX);
        const float* py = storage.Column(EParticleColumn::PosY);
        const float* pz = storage.Column(EParticleColumn::PosZ);
        const float* age = storage.Column(EParticleColumn::NormalizedAge);
        const float* size = storage.Column(EParticleColumn::SizeX);
        const float* cr = storage.Column(EParticleColumn::ColR);
        const float* cg = storage.Column(EParticleColumn::ColG);
        const float* cb = storage.Column(EParticleColumn::ColB);
        const float* ca = storage.Column(EParticleColumn::ColA);
        // Present only with Size Over Life / Size By Speed: they taper the ribbon (size / size at birth).
        const float* baseSize = storage.Column(EParticleColumn::BaseSizeX);
        const float halfWidth = 0.5f * emitter.GetProgram().Ribbon.RibbonWidth;

        float sumX = 0.0f, sumY = 0.0f, sumZ = 0.0f;
        Plu::ParticleInstanceGPU* dst = out.Data();
        for (UInt32 i = 0; i < count; ++i) {
            Plu::ParticleInstanceGPU& instance = dst[i];
            instance.PositionRotation.x = px[i];
            instance.PositionRotation.y = py[i];
            instance.PositionRotation.z = pz[i];
            instance.PositionRotation.w = age[i];
            instance.SizeAndVelocity.x = baseSize ? halfWidth * size[i] / std::max(baseSize[i], 1e-6f) : halfWidth;
            instance.SizeAndVelocity.y = 0.0f;
            instance.SizeAndVelocity.z = 0.0f;
            instance.SizeAndVelocity.w = 0.0f;
            instance.ColorRGBA8 = PackUnorm8(cr[i]) | (PackUnorm8(cg[i]) << 8) | (PackUnorm8(cb[i]) << 16) | (PackUnorm8(ca[i]) << 24);
            instance.SubUVFrameFlags = 0;
            instance.Pad0 = 0;
            instance.Pad1 = 0;
            sumX += px[i]; sumY += py[i]; sumZ += pz[i];
        }
        const float inv = 1.0f / static_cast<float>(count);
        outCentroid = Vec3(sumX * inv, sumY * inv, sumZ * inv);
    }
}

void Plu::Renderer::SyncParticleSpawners(Plu::RenderSnapshot *snapshot)
{
    PLU_PROFILE_SCOPE("Particle Spawners Sync");
    HashMap<UInt64, RenderParticleSpawner>* spawners = mParticleSpawners.Find(snapshot->SceneHandle);
    if (!spawners) {
        if (snapshot->ParticleSpawners.IsEmpty()) return;
        mParticleSpawners.Insert(snapshot->SceneHandle, {});
        spawners = mParticleSpawners.Find(snapshot->SceneHandle);
    }

    if (mLastFrameSceneHandle != snapshot->SceneHandle && mParticleSpawners.Contains(mLastFrameSceneHandle)) {
        for (auto& spawner : mParticleSpawners[mLastFrameSceneHandle]) {
            DestroyRenderParticleSpawner(spawner.second);
        }
        PLU_CORE_TRACE("Destroyed {} Particle Spawners after scene abandoned", mParticleSpawners[mLastFrameSceneHandle].Size());
        mParticleSpawners[mLastFrameSceneHandle].Clear();
    }
    mLastFrameSceneHandle = snapshot->SceneHandle;

    // The snapshot lists every live spawner of this world, so syncing is idempotent: a stale
    // snapshot rendered again, or a dropped one, changes nothing (see ParticleSpawnerRenderObject).
    // Compiled programs of this frame, by system uuid. Valid for THIS call only: main overwrites the
    // snapshot slot afterwards, so nothing below may keep a pointer into it (copy on adoption instead).
    HashMap<UInt64, const CompiledParticleSystem*> programs;
    for (const CompiledParticleSystem& program : snapshot->ParticleSystems)
        programs.InsertOrAssign(program.SystemUuid.getUUID(), &program);

    HashSet<UInt64> liveSpawners;
    for (const ParticleSpawnerRenderObject& state : snapshot->ParticleSpawners) {
        liveSpawners.Insert(state.UUID.getUUID());

        RenderParticleSpawner* existing = spawners->Find(state.UUID.getUUID());
        const bool wantsSystem = state.SystemUuid != 0;

        // The component was pointed at an asset (or lost it): the other kind of spawner is rebuilt.
        if (existing && static_cast<bool>(existing->System) != wantsSystem) {
            DestroyRenderParticleSpawner(*existing);
            spawners->Remove(state.UUID.getUUID());
            existing = nullptr;
        }

        if (wantsSystem) {
            const CompiledParticleSystem* const* found = programs.Find(state.SystemUuid.getUUID());
            if (!found) {
                // Cannot happen for a published snapshot (see RenderSnapshot::ParticleSystems), but a
                // missing program must never crash the render thread: leave the spawner as it is.
                PLU_CORE_WARN("Particle spawner {} refers to system {} that is not in the snapshot",
                              state.UUID.getUUID(), state.SystemUuid.getUUID());
                continue;
            }
            if (!existing) {
                RenderParticleSpawner created;
                created.System = CreateOwning<RenderParticleSystem>();
                spawners->Insert(state.UUID.getUUID(), created);
                existing = spawners->Find(state.UUID.getUUID());
            }
            SyncParticleSystemSpawner(state, **found, snapshot->ParticleParameterValues, *existing);
            continue;
        }

        TUsePointer<ParticleSpawner> spawner;
        if (existing) {
            spawner = existing->Spawner;
        } else {
            EngineObjectHandle spawnerHandle = mApplicationInfo->AppObjectManager->CreateObject<ParticleSpawner>();
            RenderParticleSpawner newSpawner;
            newSpawner.Spawner = mApplicationInfo->AppObjectManager->GetObjectAsOwner<ParticleSpawner>(spawnerHandle);
            newSpawner.Spawner->UUID = state.UUID;
            spawner = newSpawner.Spawner;
            spawners->Insert(state.UUID.getUUID(), newSpawner);
            PLU_CORE_TRACE("New Particles Spawner UUID: {}", state.UUID.getUUID());
        }

        // Class and transform every frame: edits and movement apply live. Transform before the
        // spawn sync, so a burst requested this frame starts at the current location.
        spawner->SetParticleClass(state.ParticleClassData);
        spawner->SetTransform(state.Location, state.LaunchDirection);
        spawner->SyncSpawnRequests(state.RequestedParticles, state.LastBurstSize);
    }

    DynamicArray<UInt64> removedSpawners;
    for (const auto& spawner : *spawners) {
        if (!liveSpawners.Contains(spawner.first)) {
            removedSpawners.PushBack(spawner.first);
        }
    }
    for (UInt64 uuid : removedSpawners) {
        DestroyRenderParticleSpawner(*spawners->Find(uuid));
        spawners->Remove(uuid);
    }
    if (spawners->IsEmpty()) {
        mParticleSpawners.Remove(snapshot->SceneHandle);
    }
}

void Plu::Renderer::SyncParticleSystemSpawner(const ParticleSpawnerRenderObject& state,
                                              const CompiledParticleSystem& program,
                                              const DynamicArray<float>& snapshotParameterValues,
                                              RenderParticleSpawner& spawner)
{
    RenderParticleSystem& system = *spawner.System;

    // New revision (or first sight): copy the program in. Emitters are matched by UUID so an edit that
    // adds/removes/reorders emitters keeps the survivors' particles; SetProgram itself only restarts an
    // emitter whose column layout or particle cap changed.
    if (system.SystemUuid != state.SystemUuid.getUUID() || system.Revision != program.Revision) {
        PLU_PROFILE_SCOPE("Particles/AdoptProgram");
        DynamicArray<TOwningPointer<ParticleEmitterInstance>> nextEmitters;
        DynamicArray<RenderParticleEmitterGPU> nextBuffers;
        DynamicArray<bool> reused;
        reused.Resize(system.Emitters.Size());
        for (UInt32 i = 0; i < reused.Size(); ++i) reused[i] = false;

        for (const CompiledEmitter& compiled : program.Emitters) {
            UInt32 match = system.Emitters.Size();
            for (UInt32 i = 0; i < system.Emitters.Size(); ++i) {
                if (!reused[i] && system.Emitters[i]->GetProgram().EmitterUuid == compiled.EmitterUuid) { match = i; break; }
            }
            if (match < system.Emitters.Size()) {
                reused[match] = true;
                nextEmitters.PushBack(system.Emitters[match]);
                nextBuffers.PushBack(system.EmitterBuffers[match]);
            } else {
                nextEmitters.PushBack(CreateOwning<ParticleEmitterInstance>());
                nextBuffers.PushBack(RenderParticleEmitterGPU());
            }
            nextEmitters[nextEmitters.Size() - 1]->SetProgram(compiled);
        }
        for (UInt32 i = 0; i < reused.Size(); ++i) {
            if (!reused[i]) system.EmitterBuffers[i].Destroy(); // emitter removed from the asset
        }
        system.Emitters = std::move(nextEmitters);
        system.EmitterBuffers = std::move(nextBuffers);
        system.ParameterDefaults = program.ParameterDefaults;
        system.ParameterLayout = program.ParameterLayout;
        system.SystemUuid = state.SystemUuid.getUUID();
        system.Revision = program.Revision;
    }

    // Lifecycle: compare the monotonic counters with what was synced last. Restart wins over clear.
    if (state.ActivationVersion != system.SyncedActivation) {
        for (auto& emitter : system.Emitters) emitter->Reset();
        for (auto& buffers : system.EmitterBuffers) buffers.ClearCounts();
        system.SyncedActivation = state.ActivationVersion;
        system.SyncedClear = state.ClearVersion;
        // Rebase: a burst requested before Play() belongs to the previous run and must not fire again.
        system.SyncedRequested = state.RequestedParticles;
        system.PendingExtraSpawn = 0;
    } else if (state.ClearVersion != system.SyncedClear) {
        for (auto& emitter : system.Emitters) emitter->ClearParticles();
        for (auto& buffers : system.EmitterBuffers) buffers.ClearCounts();
        system.SyncedClear = state.ClearVersion;
    }

    // Exactly-once bursts from SpawnParticles(): the difference to the counter seen last.
    if (state.RequestedParticles > system.SyncedRequested) {
        system.PendingExtraSpawn += static_cast<UInt32>(state.RequestedParticles - system.SyncedRequested);
        system.SyncedRequested = state.RequestedParticles;
    }

    system.State = state.EmissionState;
    system.Location = state.Location;
    system.Rotation = state.Rotation;

    // Parameter block: tolerate a count that disagrees with the program for one frame (a recompile
    // landed between the block write and the program adoption) — the executor falls back to defaults.
    system.ParameterValues.Clear();
    if (state.ParameterValueCount > 0 &&
        state.ParameterValueOffset + state.ParameterValueCount <= snapshotParameterValues.Size()) {
        system.ParameterValues.Resize(state.ParameterValueCount);
        std::memcpy(system.ParameterValues.Data(), snapshotParameterValues.Data() + state.ParameterValueOffset,
                    sizeof(float) * state.ParameterValueCount);
    }
}

Plu::ParticleDebugStats Plu::Renderer::GatherParticleDebugStats(Plu::RenderSnapshot *snapshot, float deltaTime) const
{
    PLU_PROFILE_SCOPE("Particle Debug Stats Gather");
    ParticleDebugStats stats;
    stats.SceneHandle = snapshot->SceneHandle;
    stats.DeltaTime = deltaTime;
    for (const auto& world : mParticleSpawners) {
        const bool isRenderedWorld = world.first == snapshot->SceneHandle;
        for (const auto& spawner : world.second) {
            if (spawner.second.System) {
                if (isRenderedWorld) {
                    stats.SystemSpawners.PushBack(GatherParticleSystemDebugStats(spawner.first, *spawner.second.System));
                } else {
                    stats.OtherWorldSpawners++;
                    for (const auto& emitter : spawner.second.System->Emitters) stats.OtherWorldAliveParticles += emitter->Alive();
                }
                continue;
            }
            if (!spawner.second.Spawner) continue;
            ParticleSpawnerDebugStats spawnerStats = spawner.second.Spawner->GatherDebugStats();
            if (isRenderedWorld) {
                stats.Spawners.PushBack(spawnerStats);
            } else {
                stats.OtherWorldSpawners++;
                stats.OtherWorldAliveParticles += spawnerStats.AliveParticles;
            }
        }
    }
    return stats;
}

Plu::ParticleSystemSpawnerDebugStats Plu::Renderer::GatherParticleSystemDebugStats(UInt64 spawnerUuid, const RenderParticleSystem& system) const
{
    ParticleSystemSpawnerDebugStats out;
    out.UUID = spawnerUuid;
    out.SystemUuid = system.SystemUuid;
    out.Revision = system.Revision;
    out.Location = system.Location;
    out.State = system.State;
    out.ActivationVersion = system.SyncedActivation;
    out.ClearVersion = system.SyncedClear;
    out.CompletedActivationVersion = system.CompletedActivation;
    out.SyncedRequestedParticles = system.SyncedRequested;
    out.ParameterLayout = system.ParameterLayout;
    // What the executor actually read: the snapshot block, or the defaults when it did not fit the program.
    out.UsingDefaults = system.ParameterValues.Size() != system.ParameterDefaults.Size();
    out.ParameterValues = out.UsingDefaults ? system.ParameterDefaults : system.ParameterValues;

    for (UInt32 e = 0; e < system.Emitters.Size(); ++e) {
        const ParticleEmitterInstance& emitter = *system.Emitters[e];
        const CompiledEmitter& program = emitter.GetProgram();
        const ParticleBlockStorage& storage = emitter.GetStorage();
        ParticleEmitterDebugStats emitterStats;
        emitterStats.Name = program.Name;
        emitterStats.Enabled = program.Enabled;
        emitterStats.AliveParticles = emitter.Alive();
        emitterStats.MaxParticles = program.MaxParticles;
        emitterStats.Time = emitter.GetTime();
        emitterStats.EmissionDone = emitter.IsEmissionDone();
        emitterStats.SpawnRate = program.SpawnRate;
        emitterStats.BurstCount = program.BurstCount;
        emitterStats.HasSprite = program.HasSprite();
        emitterStats.HasRibbon = program.HasRibbon();
        emitterStats.RibbonMode = program.Ribbon.RibbonMode;
        emitterStats.OrderedStorage = storage.IsOrdered();
        emitterStats.HistorySamples = storage.HistoryFloats() / kRibbonHistorySampleFloats;
        emitterStats.SpawnOpCount = program.SpawnOps.Size();
        emitterStats.UpdateOpCount = program.UpdateOps.Size();
        for (UInt32 c = 0; c < kParticleColumnCount; ++c) if (storage.UsedColumns() & (1u << c)) emitterStats.UsedColumns++;
        emitterStats.CpuBytes = storage.GetAllocatedBytes();
        if (e < system.EmitterBuffers.Size()) {
            const RenderParticleEmitterGPU& buffers = system.EmitterBuffers[e];
            emitterStats.GpuBytes = static_cast<UInt64>(buffers.Points.Capacity) * 3 * sizeof(float)
                                  + buffers.SpriteInstances.GetAllocatedBytes() + buffers.RibbonInstances.GetAllocatedBytes()
                                  + buffers.History.CapacityBytes;
        }
        emitterStats.HasBounds = emitter.ComputeBounds(emitterStats.BoundsMin, emitterStats.BoundsMax);

        const ParticleOpTimings& timings = emitter.GetLastTickOpTimings();
        auto addOps = [&](const char* stage, const DynamicArray<ParticleOp>& ops, const DynamicArray<double>& ms) {
            for (UInt32 i = 0; i < ops.Size() && i < ms.Size(); ++i) {
                ParticleOpDebugRow row;
                row.Name = String(stage) + " " + String::FromInt(i) + " " + ParticleOpName(ops[i].Code) + " -> col " +
                           String::FromInt(ops[i].Dst);
                row.Ms = static_cast<float>(ms[i]);
                emitterStats.Ops.PushBack(row);
            }
        };
        addOps("spawn", program.SpawnOps, timings.SpawnMs);
        addOps("update", program.UpdateOps, timings.UpdateMs);
        out.Emitters.PushBack(emitterStats);
    }
    return out;
}

void Plu::Renderer::DestroyRenderParticleSpawner(RenderParticleSpawner& spawner)
{
    if (spawner.Spawner) {
        mApplicationInfo->AppObjectManager->DestroyObject(spawner.Spawner->GetObjectHandle());
        spawner.Spawner = nullptr;
    }
    spawner.PointBuffer.Destroy();
    if (spawner.System) {
        // Asset-driven spawner: GL buffers per emitter. The column storage frees with the pointer.
        for (RenderParticleEmitterGPU& buffers : spawner.System->EmitterBuffers) buffers.Destroy();
        spawner.System->EmitterBuffers.Clear();
        spawner.System = nullptr;
    }
}

void Plu::Renderer::DestroyParticleSpawners()
{
    for (auto& world : mParticleSpawners) {
        for (auto& spawner : world.second) {
            DestroyRenderParticleSpawner(spawner.second);
        }
    }
    mParticleSpawners.Clear();
}

void Plu::Renderer::TickParticleSpawners(Plu::RenderSnapshot *snapshot, float deltaTime, const Matrix4& view)
{
    PLU_PROFILE_SCOPE("Particles Tick");
    SyncParticleSpawners(snapshot);
    // Per-op timing only while the Debug Particles panel asks for it (renewed every frame).
    const bool profileOps = ConsumeParticleOpTimingsRequest();
    HashMap<UInt64, RenderParticleSpawner>* spawners = mParticleSpawners.Find(snapshot->SceneHandle);

    // Feedback for main (RenderParticleLiveness.h): O(spawners), from counters the tick keeps anyway.
    ParticleLivenessFrame liveness;
    liveness.SceneHandle = snapshot->SceneHandle;

    if (spawners) {
        for (auto& entry : *spawners) {
            RenderParticleSpawner& spawner = entry.second;
            ParticleSpawnerLiveness alive;
            alive.SpawnerUuid = entry.first;

            if (spawner.System) {
                RenderParticleSystem& system = *spawner.System;
                const bool paused = system.State == EParticleEmissionState::Paused;

                ParticleTickParams params;
                params.DeltaTime = paused ? 0.0f : deltaTime; // Pause: tick with dt 0, nothing advances
                params.Location = system.Location;
                params.Rotation = system.Rotation;
                params.Emit = system.State == EParticleEmissionState::Playing;
                params.ParameterValues = system.ParameterValues.Data();
                params.ParameterValueCount = system.ParameterValues.Size();
                params.ParameterDefaults = system.ParameterDefaults.Data();
                params.ParameterDefaultCount = system.ParameterDefaults.Size();
                // A SpawnParticles() burst goes to every emitter of the system.
                params.ExtraSpawn = system.PendingExtraSpawn;
                system.PendingExtraSpawn = 0;

                bool allDone = true;
                for (UInt32 e = 0; e < system.Emitters.Size(); ++e) {
                    ParticleEmitterInstance& emitter = *system.Emitters[e];
                    emitter.SetProfileOps(profileOps);
                    emitter.Tick(params);
                    alive.AliveParticles += emitter.Alive();

                    const bool stillEmitting = system.State == EParticleEmissionState::Playing &&
                                               emitter.GetProgram().Enabled && !emitter.IsEmissionDone();
                    if (stillEmitting) alive.EmittingEmitters++;
                    // Done = nothing alive and nothing more to emit (finished, disabled or soft-stopped).
                    // A paused system is never done.
                    const bool emissionOver = emitter.IsEmissionDone() || !emitter.GetProgram().Enabled ||
                                              system.State == EParticleEmissionState::Stopped;
                    if (paused || emitter.Alive() > 0 || !emissionOver) allDone = false;

                    RenderParticleEmitterGPU& buffers = system.EmitterBuffers[e];
                    const CompiledEmitter& program = emitter.GetProgram();
                    buffers.ClearCounts();
                    // Sprite and ribbon may both be in use: the same particles packed twice, one layout each.
                    if (program.HasSprite()) {
                        PackParticleSpriteInstances(emitter, view, system.InstanceScratch, buffers.Centroid);
                        buffers.SpriteInstances.Upload(system.InstanceScratch.Data(), emitter.Alive());
                    }
                    if (program.HasRibbon()) {
                        PackParticleRibbonInstances(emitter, system.InstanceScratch, buffers.Centroid);
                        buffers.RibbonInstances.Upload(system.InstanceScratch.Data(), emitter.Alive());
                        // Per-particle trails: the storage keeps them in exactly the GPU layout (vec4 samples).
                        const ParticleBlockStorage& storage = emitter.GetStorage();
                        const UInt32 samples = storage.HistoryFloats() / kRibbonHistorySampleFloats;
                        if (samples > 0) buffers.History.Upload(storage.History(), emitter.Alive(), samples);
                    }
                    if (!program.HasSprite() && !program.HasRibbon()) {
                        // No renderer module: plain points.
                        PLU_PROFILE_SCOPE("Particles/PointPack");
                        const UInt32 count = emitter.Alive();
                        const ParticleBlockStorage& storage = emitter.GetStorage();
                        if (count == 0) {
                            buffers.Points.Count = 0;
                        } else {
                            ResizeScratch(system.PointScratch, count * 3);
                            const float* x = storage.Column(EParticleColumn::PosX);
                            const float* y = storage.Column(EParticleColumn::PosY);
                            const float* z = storage.Column(EParticleColumn::PosZ);
                            float* out = system.PointScratch.Data();
                            for (UInt32 i = 0; i < count; ++i) { out[3 * i] = x[i]; out[3 * i + 1] = y[i]; out[3 * i + 2] = z[i]; }
                            buffers.Points.Upload(out, count);
                        }
                    }
                }
                // Monotonic: the run this state belongs to has no work left.
                if (allDone) system.CompletedActivation = system.SyncedActivation;
                alive.CompletedActivationVersion = system.CompletedActivation;
            } else if (spawner.Spawner) {
                spawner.Spawner->TickParticles(deltaTime);
                spawner.PointBuffer.Upload(spawner.Spawner->GetPositions(), spawner.Spawner->GetAliveCount());
                alive.AliveParticles = spawner.Spawner->GetAliveCount();
            }
            liveness.Spawners.PushBack(alive);
        }
    }
    PublishParticleLiveness(std::move(liveness));
}

void Plu::Renderer::RenderParticles(Plu::RenderSnapshot *snapshot, const Matrix4 &view, const Matrix4 &viewProj)
{
    HashMap<UInt64, RenderParticleSpawner>* spawners = mParticleSpawners.Find(snapshot->SceneHandle);
    if (!spawners || spawners->IsEmpty()) return;

    PLU_PROFILE_SCOPE("Renderer::RenderParticles");
    PLU_PROFILE_SCOPE_GPU("Renderer::RenderParticles");

    TUsePointer<ShaderProgram> shader = mApplicationInfo->AppShaderManager->GetShaderProgram(EngineAssets::ParticlePointProgram);
    if (!shader) return;
    if (!shader->IsLoaded()) {
        // Lazy compile on the render thread (same as DebugLine); particles show up next frame.
        mApplicationInfo->AppShaderManager->LoadShader(shader->Uuid);
        return;
    }

    shader->Bind();
    shader->SetMatrix4Uniform("uViewProj", viewProj);
    shader->SetMatrix4Uniform("uView", view);
    // Set here rather than trusting the per-frame broadcast: without a directional light it is
    // never written, and the shader reads it only when the ShadowData block has cascades anyway.
    shader->SetVec3Uniform("dirLightDir", snapshot->HasDirLight ? snapshot->DirLight.Direction : Vec3(0.0f, -1.0f, 0.0f));

    // Opaque points: depth test and depth writes as for the rest of the main pass. Receiving
    // shadows reads the cascade atlas (unit kShadowTextureUnit) and the ShadowData block, both
    // bound for the whole main pass by RenderSnapshot.
    for (const auto& spawner : *spawners) {
        if (!spawner.second.Spawner || spawner.second.PointBuffer.Count == 0) continue;
        const ParticleClass& particleClass = spawner.second.Spawner->GetParticleClass();
        shader->SetVec3Uniform("uColor", particleClass.Color);
        shader->SetIntUniform("uReceiveShadows", particleClass.ReceivesShadow() ? 1 : 0);
        shader->SetFloatUniform("uShadowSize", particleClass.ShadowSize);
        // glPointSize rejects sizes <= 0 with GL_INVALID_VALUE; the driver clamps the top end.
        glPointSize(std::max(particleClass.PointSize, 1.0f));
        spawner.second.PointBuffer.Draw();
    }
    // Asset-driven emitters without a renderer module: one point cloud per emitter, coloured by its first
    // particle. No shadows (DrawParticleShadowCasters and the receive path are legacy-only).
    for (const auto& spawner : *spawners) {
        if (!spawner.second.System) continue;
        const RenderParticleSystem& system = *spawner.second.System;
        shader->SetIntUniform("uReceiveShadows", 0);
        shader->SetFloatUniform("uShadowSize", 0.0f);
        for (UInt32 e = 0; e < system.Emitters.Size(); ++e) {
            if (system.EmitterBuffers[e].Points.Count == 0) continue;
            const ParticleBlockStorage& storage = system.Emitters[e]->GetStorage();
            shader->SetVec3Uniform("uColor", Vec3(storage.Column(EParticleColumn::ColR)[0],
                                                 storage.Column(EParticleColumn::ColG)[0],
                                                 storage.Column(EParticleColumn::ColB)[0]));
            glPointSize(3.0f);
            system.EmitterBuffers[e].Points.Draw();
        }
    }
    glPointSize(1.0f);
}

void Plu::Renderer::RenderTransparentParticles(Plu::RenderSnapshot *snapshot, const Matrix4 &view, const Matrix4 &projection)
{
    HashMap<UInt64, RenderParticleSpawner>* spawners = mParticleSpawners.Find(snapshot->SceneHandle);
    if (!spawners || spawners->IsEmpty()) return;

    // Draw list: every sprite / ribbon emitter with something to draw, back to front by the distance of
    // its particles' centroid. Emitters, never particles — Additive does not care about order, and
    // AlphaBlend within one emitter is accepted as unsorted.
    mParticleSpriteDraws.Clear();
    for (const auto& spawner : *spawners) {
        if (!spawner.second.System) continue;
        const RenderParticleSystem& system = *spawner.second.System;
        for (UInt32 e = 0; e < system.Emitters.Size(); ++e) {
            const RenderParticleEmitterGPU& buffers = system.EmitterBuffers[e];
            const CompiledEmitter& program = system.Emitters[e]->GetProgram();
            const Vec3 toCamera = buffers.Centroid - snapshot->CameraLocation;
            const float distanceSq = glm::dot(toCamera, toCamera);
            // A ribbon needs two points: two particles (PerEmitter) or a trail (PerParticle).
            if (program.HasRibbon()) {
                const bool perEmitter = program.Ribbon.RibbonMode == EParticleRibbonMode::PerEmitter;
                if (perEmitter ? buffers.RibbonInstances.Count >= 2 : buffers.History.Particles > 0)
                    mParticleSpriteDraws.PushBack(ParticleSpriteDraw{ &buffers, &program.Ribbon, distanceSq, 0 });
            }
            if (program.HasSprite() && buffers.SpriteInstances.Count > 0)
                mParticleSpriteDraws.PushBack(ParticleSpriteDraw{ &buffers, &program.Sprite, distanceSq, 1 });
        }
    }
    if (mParticleSpriteDraws.IsEmpty()) return;

    PLU_PROFILE_SCOPE("Renderer::RenderTransparentParticles");
    PLU_PROFILE_SCOPE_GPU("Renderer::RenderTransparentParticles");

    // Lazy compile on the render thread (same as DebugLine); a program that is not ready yet skips its
    // emitters this frame.
    auto resolveProgram = [this](UInt64 uuid) -> TUsePointer<ShaderProgram> {
        TUsePointer<ShaderProgram> program = mApplicationInfo->AppShaderManager->GetShaderProgram(uuid);
        if (!program) return nullptr;
        if (!program->IsLoaded()) {
            mApplicationInfo->AppShaderManager->LoadShader(program->Uuid);
            return nullptr;
        }
        return program;
    };
    TUsePointer<ShaderProgram> spriteShader = resolveProgram(EngineAssets::ParticleSpriteProgram);
    TUsePointer<ShaderProgram> ribbonShader = resolveProgram(EngineAssets::ParticleRibbonProgram);

    // Back to front; within one emitter (equal distance) the ribbon first, so its sprites sit on top.
    mParticleSpriteDraws.Sort([](const ParticleSpriteDraw& a, const ParticleSpriteDraw& b) {
        return a.DistanceSq != b.DistanceSq ? a.DistanceSq > b.DistanceSq : a.Layer < b.Layer;
    });

    const Matrix4 viewProj = projection * view;
    if (spriteShader) {
        spriteShader->Bind();
        spriteShader->SetMatrix4Uniform("uView", view);
        spriteShader->SetMatrix4Uniform("uProjection", projection);
    }
    if (ribbonShader) {
        ribbonShader->Bind();
        ribbonShader->SetMatrix4Uniform("uViewProj", viewProj);
        ribbonShader->SetVec3Uniform("uCameraPos", snapshot->CameraLocation);
    }

    // Transparent: test against the opaque depth, never write it. Quads and ribbons are camera-facing and
    // may come out mirrored, so no face culling either.
    const GLboolean cullWasOn = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glBindVertexArray(mParticleSpriteVao);

    for (const ParticleSpriteDraw& draw : mParticleSpriteDraws) {
        const ParticleRenderParams& params = *draw.Params;
        const bool isRibbon = params.Kind == EParticleRendererKind::Ribbon;
        ShaderProgram* shader = isRibbon ? ribbonShader.GetRaw() : spriteShader.GetRaw();
        if (!shader) continue;

        // Texture: asset data and GL texture both resolved without I/O; a miss requests the load and
        // skips the emitter for this frame (same rule as RenderFromMaterial).
        TUsePointer<Texture> texture;
        if (params.TextureUuid != 0) {
            TUsePointer<TextureInfo> textureInfo = mApplicationInfo->AppAssetManager->GetAssetDataNoLoad(PluUUID(params.TextureUuid));
            if (!textureInfo) {
                mApplicationInfo->AppAssetManager->RequestAssetDataLoad(PluUUID(params.TextureUuid));
                continue;
            }
            texture = mApplicationInfo->AppRenderingManager->GetTextureForInfo(textureInfo);
            if (!texture) {
                mApplicationInfo->AppRenderingManager->RequestTextureFromInfo(textureInfo);
                continue;
            }
            texture->Bind(0);
        }

        shader->Bind();
        shader->SetIntUniform("uHasTexture", texture ? 1 : 0);

        // Destination alpha stays as it is (ZERO, ONE): the main buffer is RGBA and ImGui shows it with
        // blending, so a sprite lowering its alpha would punch a see-through hole into the viewport image.
        if (params.Blend == EParticleBlendMode::Additive) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
        else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

        if (isRibbon) {
            draw.Buffers->RibbonInstances.Bind();
            if (params.RibbonMode == EParticleRibbonMode::PerEmitter) {
                // One strip through every live particle, oldest first (ordered storage).
                const UInt32 points = draw.Buffers->RibbonInstances.Count;
                shader->SetIntUniform("uRibbonMode", 1);
                shader->SetIntUniform("uPointCount", static_cast<int>(points));
                glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(points * 2));
            } else {
                // One strip per particle through its trail; instances never join into one strip.
                const ParticleRibbonHistoryBuffer& history = draw.Buffers->History;
                history.Bind();
                shader->SetIntUniform("uRibbonMode", 0);
                shader->SetIntUniform("uPointCount", static_cast<int>(history.SamplesPerParticle));
                glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(history.SamplesPerParticle * 2),
                                      static_cast<GLsizei>(history.Particles));
            }
        } else {
            draw.Buffers->SpriteInstances.Bind();
            shader->SetIntUniform("uFacingMode", params.Facing == EParticleFacingMode::VelocityStretched ? 1 : 0);
            shader->SetFloatUniform("uStretchFactor", params.StretchFactor);
            shader->SetIntUniform("uSubUVColumns", static_cast<int>(params.SubUVColumns));
            shader->SetIntUniform("uSubUVRows", static_cast<int>(params.SubUVRows));
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(draw.Buffers->SpriteInstances.Count));
        }
        snapshot->StatDrawCalls++;
    }

    // Restore what OpenGLRenderState set once for the whole app.
    glBindVertexArray(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    if (cullWasOn) glEnable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Plu::Renderer::DrawParticleShadowCasters(Plu::RenderSnapshot *snapshot, const Matrix4 &viewProj,
                                              const Matrix4 &projection, Int32 resolution)
{
    if (!mParticleShadowReady) return;
    HashMap<UInt64, RenderParticleSpawner>* spawners = mParticleSpawners.Find(snapshot->SceneHandle);
    if (!spawners || spawners->IsEmpty()) return;

    PLU_PROFILE_SCOPE("Renderer::DrawParticleShadowCasters");
    PLU_PROFILE_SCOPE_GPU("Renderer::DrawParticleShadowCasters");

    mParticleShadowShader->Bind();
    mParticleShadowShader->SetMatrix4Uniform("uViewProj", viewProj);
    // Texels per metre at w == 1. projection[0][0] is 1 / half-width for the ortho cascades and
    // cot(fov/2) for the square spot frusta — either way half the target covers 1 / it metres at w == 1.
    mParticleShadowShader->SetFloatUniform("uSizeScale", projection[0][0] * 0.5f * static_cast<float>(resolution));

    for (const auto& spawner : *spawners) {
        if (!spawner.second.Spawner || spawner.second.PointBuffer.Count == 0) continue;
        const ParticleClass& particleClass = spawner.second.Spawner->GetParticleClass();
        if (!particleClass.CastsShadow() || particleClass.ShadowSize <= 0.0f) continue;
        mParticleShadowShader->SetFloatUniform("uShadowSize", particleClass.ShadowSize);
        spawner.second.PointBuffer.Draw();
    }
}

Plu::TUsePointer<Plu::FrameBuffer> Plu::Renderer::GetMainFrameBuffer()
{
    return mMainBuffer;
}

void Plu::Renderer::Initialize(ApplicationInfo *applicationInfo)
{
    mApplicationInfo = applicationInfo;
    EngineObjectHandle hdl = mApplicationInfo->AppObjectManager->CreateObject<FrameBuffer>();
    mMainBuffer = mApplicationInfo->AppObjectManager->GetObjectAsOwner<FrameBuffer>(hdl);
    TUsePointer<IWindow> window = mApplicationInfo->AppWindow;
    mMainBuffer->Create(window->GetWidth(), window->GetHeight(), mApplicationInfo->AppObjectManager, FrameBufferType::ColorDepth);

    // GL 4.5 guarantees at least 16 texture image units per stage, so kShadowTextureUnit (15) is
    // always legal — but a driver reporting less would silently drop every shadow lookup, which
    // is exactly the kind of failure worth naming out loud rather than debugging from pixels.
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    mMaxTextureSize = static_cast<Int32>(maxTextureSize);

    GLint maxTextureUnits = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTextureUnits);
    if (maxTextureUnits <= static_cast<GLint>(kShadowTextureUnit)) {
        PLU_CORE_ERROR("Renderer::Initialize - GL_MAX_TEXTURE_IMAGE_UNITS is {}, but the shadow map array needs unit {}. "
                       "Directional shadows will not sample correctly.", maxTextureUnits, kShadowTextureUnit);
    }

    // Shadow atlas + its framebuffer, created eagerly at the default settings — the GL context is
    // on the render thread here, so the frame path only allocates GL objects when a light's
    // settings actually change the atlas geometry.
    {
        DirectionalLightShadowSettings defaults;
        defaults.Resolution = kDefaultShadowResolution;
        BuildCascadeAtlas(ClampShadowSettings(defaults));
    }

    // Spot shadow atlas — same deal, but its geometry is a pair of engine constants rather than
    // a per-light setting, so it is created once here and never rebuilt.
    RecreateSpotShadowResources();

    // Comparison sampler for the lighting pass. LINEAR + COMPARE_REF_TO_TEXTURE is what turns a
    // single texture() fetch into a bilinear 2x2 depth comparison (hardware PCF); the white
    // border makes everything outside a cascade read as lit.
    mShadowCompareSampler.Create();
    mShadowCompareSampler.SetFilter(GL_LINEAR, GL_LINEAR);
    constexpr float kShadowBorder[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    mShadowCompareSampler.SetWrap(GL_CLAMP_TO_BORDER, GL_CLAMP_TO_BORDER, kShadowBorder);
    mShadowCompareSampler.SetCompareMode(GL_LEQUAL);

    // VAO/VBO debugowej geometrii fizyki — kontekst GL jest tu na wątku renderu.
    // Layout per wierzchołek: pos(3) + color(3), stride 6 floatów.
    glGenVertexArrays(1, &mDebugVao);
    glGenBuffers(1, &mDebugVbo);
    glBindVertexArray(mDebugVao);
    glBindBuffer(GL_ARRAY_BUFFER, mDebugVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    // Empty VAO for the attribute-less editor grid pass (see mGridVao in Renderer.h).
    glGenVertexArrays(1, &mGridVao);
    // Same for the attribute-less particle sprite draw (see mParticleSpriteVao).
    glGenVertexArrays(1, &mParticleSpriteVao);

    mSkeletalMatricesBuffer.Create(100);
    mInstanceBuffer.Create(100);

    // Shadow parameter block (binding 2). Bound once here — glBindBufferBase survives program
    // switches, and the buffer object is never reallocated (Update rewrites it in place), so
    // the binding stays valid for the whole run.
    mShadowDataBuffer.Create();
    mShadowDataBuffer.BindBase(2);

    // Spot light blocks (UBO 4, SSBO 5, SSBO 6). Allocated at the hard cap and bound once: MAIN
    // never sends more than kMaxVisibleSpotLights, so these buffers never reallocate and their
    // indexed bindings stay valid for the whole run (a Resize would create a new buffer ID and
    // silently leave the binding point holding the deleted one).
    mSpotLightDataBuffer.Create();
    mSpotLightDataBuffer.BindBase(4);
    mSpotLightBuffer.Create(kMaxVisibleSpotLights);
    mSpotLightBuffer.BindBase(5);
    mSpotLightIndexBuffer.Create(kMaxVisibleSpotLights);
    mSpotLightIndexBuffer.BindBase(6);
}

void Plu::Renderer::RecreateSpotShadowResources()
{
    if (mSpotShadowResolution == kSpotShadowResolution
        && mSpotShadowSlotCount == kMaxSpotShadowSlots
        && mSpotShadowArray) {
        return;
    }

    DestroySpotShadowResources();

    CheckShadowGLError("Renderer::RecreateSpotShadowResources (entry)");

    EngineObjectHandle textureHandle = mApplicationInfo->AppObjectManager->CreateObject<Texture>();
    mSpotShadowArray = mApplicationInfo->AppObjectManager->GetObjectAsOwner<Texture>(textureHandle);
    if (!mSpotShadowArray->CreateDepthArray(kSpotShadowResolution, kSpotShadowResolution, kMaxSpotShadowSlots)
        || !CheckShadowGLError("Renderer::RecreateSpotShadowResources (depth array)")) {
        PLU_CORE_ERROR("Renderer::RecreateSpotShadowResources - Failed to create the spot shadow atlas ({}x{}, {} slots)",
                       kSpotShadowResolution, kSpotShadowResolution, kMaxSpotShadowSlots);
        DestroySpotShadowResources();
        return;
    }

    mSpotShadowFrameBuffers.Reserve(static_cast<UInt32>(kMaxSpotShadowSlots));
    for (Int32 slot = 0; slot < kMaxSpotShadowSlots; slot++) {
        EngineObjectHandle fbHandle = mApplicationInfo->AppObjectManager->CreateObject<FrameBuffer>();
        TOwningPointer<FrameBuffer> fb = mApplicationInfo->AppObjectManager->GetObjectAsOwner<FrameBuffer>(fbHandle);
        // Same reasoning as the cascade layers: a framebuffer that failed to create silently
        // no-ops its Clear()/Bind(), so the slot would never be written and every receiver
        // sampling it would read "occluded". Drop spot shadows entirely instead.
        if (!fb->CreateWithDepthTextureLayer(mSpotShadowArray, slot, mApplicationInfo->AppObjectManager)
            || !CheckShadowGLError("Renderer::RecreateSpotShadowResources (slot framebuffer)")) {
            PLU_CORE_ERROR("Renderer::RecreateSpotShadowResources - Failed to create the framebuffer for spot shadow slot {} — spot shadows disabled", slot);
            mApplicationInfo->AppObjectManager->DestroyObject(fb->GetObjectHandle());
            fb->Destroy();
            DestroySpotShadowResources();
            return;
        }
        mSpotShadowFrameBuffers.PushBack(fb);
    }

    mSpotShadowResolution = kSpotShadowResolution;
    mSpotShadowSlotCount  = kMaxSpotShadowSlots;

    PLU_CORE_INFO("Spot shadow resources ready: {}x{} D32F array, {} slots", kSpotShadowResolution, kSpotShadowResolution, kMaxSpotShadowSlots);
}

void Plu::Renderer::DestroySpotShadowResources()
{
    // Framebuffers first — they reference the atlas texture and must not outlive it.
    for (UInt32 s = 0; s < mSpotShadowFrameBuffers.Size(); s++) {
        if (!mSpotShadowFrameBuffers[s]) continue;
        mApplicationInfo->AppObjectManager->DestroyObject(mSpotShadowFrameBuffers[s]->GetObjectHandle());
        mSpotShadowFrameBuffers[s]->Destroy();
        mSpotShadowFrameBuffers[s] = nullptr;
    }
    mSpotShadowFrameBuffers.Clear();

    if (mSpotShadowArray) {
        mApplicationInfo->AppObjectManager->DestroyObject(mSpotShadowArray->GetObjectHandle());
        mSpotShadowArray->Destroy();
        mSpotShadowArray = nullptr;
    }

    mSpotShadowResolution = -1;
    mSpotShadowSlotCount  = -1;
}

void Plu::Renderer::UnbindSpotShadowTexture()
{
    glActiveTexture(GL_TEXTURE0 + kSpotShadowTextureUnit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    SamplerObject::Unbind(kSpotShadowTextureUnit);
}

void Plu::Renderer::RequestSpotShadowView(Int32 slot)
{
    mSpotShadowViewRequest.store(slot, std::memory_order_relaxed);
}

Plu::TUsePointer<Plu::Texture> Plu::Renderer::GetSpotShadowView()
{
    return mSpotShadowView;
}

void Plu::Renderer::UpdateSpotShadowSlotView()
{
    const Int32 slot = mSpotShadowViewRequest.load(std::memory_order_relaxed);
    if (slot < 0 || !mSpotShadowArray || slot >= mSpotShadowSlotCount) return;

    PLU_PROFILE_SCOPE("Renderer::UpdateSpotShadowSlotView");

    if (!mSpotShadowView || mSpotShadowView->GetWidth() != mSpotShadowResolution) {
        if (mSpotShadowView) {
            mApplicationInfo->AppObjectManager->DestroyObject(mSpotShadowView->GetObjectHandle());
            mSpotShadowView->Destroy();
            mSpotShadowView = nullptr;
        }
        EngineObjectHandle handle = mApplicationInfo->AppObjectManager->CreateObject<Texture>();
        mSpotShadowView = mApplicationInfo->AppObjectManager->GetObjectAsOwner<Texture>(handle);
        mSpotShadowView->CreateDepth(mSpotShadowResolution, mSpotShadowResolution);
    }

    // Straight D32F -> D32F image copy; the ImGui backend can only bind a plain GL_TEXTURE_2D.
    glCopyImageSubData(mSpotShadowArray->GetID(), GL_TEXTURE_2D_ARRAY, 0, 0, 0, slot,
                       mSpotShadowView->GetID(), GL_TEXTURE_2D, 0, 0, 0, 0,
                       mSpotShadowResolution, mSpotShadowResolution, 1);
}

void Plu::Renderer::BuildCascadeAtlas(const DirectionalLightShadowSettings& Settings)
{
    const UInt32 cascadeCount = static_cast<UInt32>(Settings.CascadeCount);

    mCascadeResolutions.Clear();
    mCascadeResolutions.Resize(cascadeCount);
    mCascadeAtlasRects.Clear();
    mCascadeAtlasRects.Resize(cascadeCount);

    Int32 atlasWidth  = 0;
    Int32 atlasHeight = 0;
    Int32 baseResolution = Settings.Resolution;

    // Halve the base resolution until the atlas fits the driver's texture limit. Many cascades at
    // a high resolution stack into a very tall atlas (8192 base x 6 cascades wants 22528 rows,
    // past the usual 16384 cap), and silently dropping shadows for a setting the details panel
    // happily offers is worse than quietly rendering them one step softer.
    while (true)
    {
        ComputeCascadeResolutions(baseResolution, Settings.CascadeCount, Settings.ResolutionFalloff,
                                  mCascadeResolutions.Data());
        BuildShadowAtlasLayout(mCascadeResolutions.Data(), Settings.CascadeCount,
                               mCascadeAtlasRects.Data(), atlasWidth, atlasHeight);

        const bool fits = mMaxTextureSize <= 0
                       || (atlasWidth <= mMaxTextureSize && atlasHeight <= mMaxTextureSize);
        if (fits || baseResolution <= kMinCascadeResolution) break;

        baseResolution /= 2;
        PLU_CORE_WARN("Renderer::BuildCascadeAtlas - Shadow atlas {}x{} exceeds GL_MAX_TEXTURE_SIZE ({}); "
                      "dropping the base shadow resolution to {}",
                      atlasWidth, atlasHeight, mMaxTextureSize, baseResolution);
    }

    RecreateShadowResources(atlasWidth, atlasHeight);
}

void Plu::Renderer::RecreateShadowResources(Int32 AtlasWidth, Int32 AtlasHeight)
{
    if (AtlasWidth == mShadowAtlasWidth && AtlasHeight == mShadowAtlasHeight && mShadowAtlas) {
        return;
    }

    DestroyShadowResources();

    // Anything still in the error queue would otherwise be blamed on the calls below.
    CheckShadowGLError("Renderer::RecreateShadowResources (entry)");

    // Last line of defence: BuildCascadeAtlas already shrinks the base resolution until the atlas
    // fits, so reaching this means even kMinCascadeResolution was too much for the driver.
    if (mMaxTextureSize > 0 && (AtlasWidth > mMaxTextureSize || AtlasHeight > mMaxTextureSize)) {
        PLU_CORE_ERROR("Renderer::RecreateShadowResources - Shadow atlas {}x{} exceeds GL_MAX_TEXTURE_SIZE ({}). "
                       "Lower ShadowCascadeCount — directional shadows disabled.",
                       AtlasWidth, AtlasHeight, mMaxTextureSize);
        return;
    }

    // The framebuffer owns its depth texture (FrameBufferType::DepthOnly), so the atlas is simply
    // that texture — no separate allocation, and no way for the two to disagree on size.
    // D32F, not D16: the depth range is no longer padded by a 50 m near margin (GL_DEPTH_CLAMP
    // replaced it), so the extra bits go straight into fighting acne instead of covering slack.
    EngineObjectHandle fbHandle = mApplicationInfo->AppObjectManager->CreateObject<FrameBuffer>();
    mShadowAtlasFrameBuffer = mApplicationInfo->AppObjectManager->GetObjectAsOwner<FrameBuffer>(fbHandle);
    // A framebuffer that failed to create must NOT be kept: FrameBuffer::Clear() and Bind()
    // silently no-op / bind framebuffer 0 on an invalid object, so the atlas would never be
    // cleared nor rendered — and an uncleared depth map reads as "everything is occluded", i.e. a
    // fully black scene. Bail out of shadows entirely instead.
    if (!mShadowAtlasFrameBuffer->CreateDepthOnly(AtlasWidth, AtlasHeight, mApplicationInfo->AppObjectManager)
        || !CheckShadowGLError("Renderer::RecreateShadowResources (atlas framebuffer)")) {
        PLU_CORE_ERROR("Renderer::RecreateShadowResources - Failed to create the shadow atlas framebuffer ({}x{}) — directional shadows disabled",
                       AtlasWidth, AtlasHeight);
        DestroyShadowResources();
        return;
    }

    mShadowAtlas = mShadowAtlasFrameBuffer->GetDepthTexture();
    if (!mShadowAtlas || !mShadowAtlas->IsValid()) {
        PLU_CORE_ERROR("Renderer::RecreateShadowResources - The shadow atlas framebuffer has no depth texture — directional shadows disabled");
        DestroyShadowResources();
        return;
    }

    mShadowAtlasWidth  = AtlasWidth;
    mShadowAtlasHeight = AtlasHeight;

    PLU_CORE_INFO("Shadow resources ready: {}x{} D32F atlas ({:.1f} MB)",
                  AtlasWidth, AtlasHeight,
                  static_cast<double>(AtlasWidth) * AtlasHeight * 4.0 / (1024.0 * 1024.0));
}

void Plu::Renderer::UnbindShadowTexture()
{
    glActiveTexture(GL_TEXTURE0 + kShadowTextureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);
    SamplerObject::Unbind(kShadowTextureUnit);
}

void Plu::Renderer::RequestShadowCascadeView(Int32 layer)
{
    mShadowLayerViewRequest.store(layer, std::memory_order_relaxed);
}

Plu::TUsePointer<Plu::Texture> Plu::Renderer::GetShadowCascadeView()
{
    return mShadowLayerView;
}

void Plu::Renderer::UpdateShadowLayerView()
{
    const Int32 layer = mShadowLayerViewRequest.load(std::memory_order_relaxed);
    if (layer < 0 || !mShadowAtlas || layer >= static_cast<Int32>(mCascadeAtlasRects.Size())) return;

    PLU_PROFILE_SCOPE("Renderer::UpdateShadowLayerView");

    const ShadowAtlasRect& rect = mCascadeAtlasRects[static_cast<UInt32>(layer)];
    if (rect.Size <= 0) return;

    // Destination is rebuilt whenever the size stops matching, so the viewer keeps working across
    // a resolution change — and across switching to a cascade of a DIFFERENT resolution, which is
    // the normal case now that the atlas is mixed.
    if (!mShadowLayerView || mShadowLayerView->GetWidth() != rect.Size) {
        if (mShadowLayerView) {
            mApplicationInfo->AppObjectManager->DestroyObject(mShadowLayerView->GetObjectHandle());
            mShadowLayerView->Destroy();
            mShadowLayerView = nullptr;
        }
        EngineObjectHandle handle = mApplicationInfo->AppObjectManager->CreateObject<Texture>();
        mShadowLayerView = mApplicationInfo->AppObjectManager->GetObjectAsOwner<Texture>(handle);
        mShadowLayerView->CreateDepth(rect.Size, rect.Size);
    }

    // Straight image copy of this cascade's rect — no framebuffer, no shader, no format
    // conversion. Both textures are D32F, so the driver can move the whole square in one go.
    glCopyImageSubData(mShadowAtlas->GetID(), GL_TEXTURE_2D, 0, rect.X, rect.Y, 0,
                       mShadowLayerView->GetID(), GL_TEXTURE_2D, 0, 0, 0, 0,
                       rect.Size, rect.Size, 1);
}

void Plu::Renderer::DestroyShadowResources()
{
    // Drop the observer BEFORE the framebuffer goes: the atlas texture is owned by it, so this
    // pointer dangles the moment Destroy() runs.
    mShadowAtlas = nullptr;

    if (mShadowAtlasFrameBuffer) {
        mApplicationInfo->AppObjectManager->DestroyObject(mShadowAtlasFrameBuffer->GetObjectHandle());
        mShadowAtlasFrameBuffer->Destroy();
        mShadowAtlasFrameBuffer = nullptr;
    }

    mShadowAtlasWidth  = -1;
    mShadowAtlasHeight = -1;
}

void Plu::Renderer::ResolveSnapshotMeshes(Plu::RenderSnapshot* snapshot)
{
    PLU_PROFILE_SCOPE("Renderer::ResolveSnapshotMeshes");
    const UInt32 staticBatchCount = snapshot->StaticMeshBatches.Size();
    mResolvedBatchMeshes.Clear();
    mResolvedBatchMeshes.Reserve(staticBatchCount);
    for (UInt32 i = 0; i < staticBatchCount; i++) {
        mResolvedBatchMeshes.PushBack(mApplicationInfo->AppAssetManager->GetAssetDataNoLoad(snapshot->StaticMeshBatches[i].MeshUUID));
    }

    const UInt32 skeletalMeshCount = snapshot->SkeletalMeshRenderObjects.Size();
    mResolvedSkeletalMeshes.Clear();
    mResolvedSkeletalMeshes.Reserve(skeletalMeshCount);
    for (UInt32 i = 0; i < skeletalMeshCount; i++) {
        mResolvedSkeletalMeshes.PushBack(mApplicationInfo->AppAssetManager->GetAssetDataNoLoad(snapshot->SkeletalMeshRenderObjects[i].MeshUUID));
    }
}

Plu::DirectionalLightShadowSettings Plu::Renderer::ClampShadowSettings(const DirectionalLightShadowSettings& Settings)
{
    // The settings come straight from a details panel, so anything can be in them. Clamping
    // here (render thread, one place) keeps every consumer — cascade math, GL allocation,
    // the UBO — free of defensive checks.
    DirectionalLightShadowSettings clamped = Settings;

    clamped.CascadeCount   = glm::clamp(clamped.CascadeCount, 1, kMaxShadowCascades);
    clamped.ShadowDistance = glm::clamp(clamped.ShadowDistance, 1.0f, kCameraFarClip);
    // 0 means "uniform"; beyond the cascade count every step lands on the same cascade, so
    // anything larger is the same thing said louder.
    clamped.ResolutionFalloff = glm::clamp(clamped.ResolutionFalloff, 0, kMaxShadowCascades);
    clamped.SplitLambda    = glm::clamp(clamped.SplitLambda, 0.0f, 1.0f);
    clamped.NormalBias     = glm::clamp(clamped.NormalBias, 0.0f, 16.0f);
    clamped.DepthBias      = glm::clamp(clamped.DepthBias, 0.0f, 1.0f);
    clamped.PcfRadius      = glm::clamp(clamped.PcfRadius, 0.0f, 8.0f);
    // Auto mode resolves to a concrete tap count HERE rather than in the shader, so everything
    // downstream (UBO, stats, anyone reading mShadowSettings) sees the number actually sampled.
    clamped.PcfTapCount    = clamped.PcfAutoTaps
                           ? ComputeAutoPcfTapCount(clamped.PcfRadius)
                           : glm::clamp(clamped.PcfTapCount, 1, kMaxShadowPcfTaps);
    clamped.CascadeBlend   = glm::clamp(clamped.CascadeBlend, 0.0f, 0.5f);

    // Contact shadows. The length cap is deliberate: a screen-space march is only trustworthy
    // over a short distance — past a metre the samples spread far enough apart to step over
    // ordinary geometry, and what the ray misses reads as a hole in the shadow, not as softness.
    clamped.ContactShadowSteps     = glm::clamp(clamped.ContactShadowSteps, 4, kMaxContactShadowSteps);
    clamped.ContactShadowLength    = glm::clamp(clamped.ContactShadowLength, 0.0f, 1.0f);
    clamped.ContactShadowThickness = glm::clamp(clamped.ContactShadowThickness, 0.001f, 1.0f);
    clamped.ContactShadowBias      = glm::clamp(clamped.ContactShadowBias, 0.0f, 0.5f);

    // Resolution snaps to a power-of-two step rather than clamping to a range: the array is
    // reallocated whenever it changes, and dragging a slider through arbitrary values would
    // reallocate ~67 MB of VRAM per frame.
    constexpr Int32 kAllowedResolutions[] = {512, 1024, 2048, 4096, 8192};
    Int32 best = kAllowedResolutions[0];
    Int32 bestDistance = std::abs(clamped.Resolution - best);
    for (Int32 candidate : kAllowedResolutions) {
        const Int32 distance = std::abs(clamped.Resolution - candidate);
        if (distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    clamped.Resolution = best;

    return clamped;
}

bool Plu::Renderer::ResolveDepthShaders()
{
    // Particle shadow casters first: independent of the mesh depth shaders, so a mesh depth shader
    // still compiling does not also hold the particle program back.
    mParticleShadowShader = mApplicationInfo->AppShaderManager->GetShaderProgram(EngineAssets::ParticleShadowProgram);
    if (mParticleShadowShader && !mParticleShadowShader->IsLoaded()) {
        mApplicationInfo->AppShaderManager->LoadShader(EngineAssets::ParticleShadowProgram);
    }
    mParticleShadowReady = mParticleShadowShader && mParticleShadowShader->IsLoaded();

    // Shader głębi instancingu (tylko pozycja, SSBO InstanceMatrices) dla static meshy — leniwa
    // kompilacja na wątku renderu. Depth pass jest silnikowy (nie opt-in per materiał jak główny
    // pass), a SSBO instancji jest już wypełniony i zbindowany (Renderer::mInstanceBuffer) dla
    // wszystkich batchy niezależnie od tego, czy materiał widocznego passu wspiera instancing.
    mDepthShader = mApplicationInfo->AppShaderManager->GetShaderProgram(EngineAssets::OnlyPositionInstancedShader);
    mSkeletalDepthReady = false;
    if (!mDepthShader) return false;
    if (!mDepthShader->IsLoaded()) {
        mApplicationInfo->AppShaderManager->LoadShader(EngineAssets::OnlyPositionInstancedShader);
        return false; // gotowe w kolejnej klatce
    }

    // Skinowany wariant — ładowany niezależnie: jeśli jeszcze nie gotowy, pomijamy tylko cienie
    // skeletalne (static-owe i tak lecą), a nie cały pass.
    mSkeletalDepthShader = mApplicationInfo->AppShaderManager->GetShaderProgram(EngineAssets::OnlyPositionSkeletalShader);
    if (mSkeletalDepthShader && !mSkeletalDepthShader->IsLoaded()) {
        mApplicationInfo->AppShaderManager->LoadShader(EngineAssets::OnlyPositionSkeletalShader);
    }
    mSkeletalDepthReady = mSkeletalDepthShader && mSkeletalDepthShader->IsLoaded();
    return true;
}

Plu::TUsePointer<Plu::ShaderProgram> Plu::Renderer::ResolveDepthProgram(MaterialInfo* materialInfo)
{
    if (!materialInfo || !mDepthShader || !mDepthShader->IsLoaded()) return nullptr;

    TUsePointer<ShaderProgram> materialProgram = mApplicationInfo->AppShaderManager->GetShaderProgram(materialInfo->shaderProgram);
    if (!materialProgram || !materialProgram->IsLoaded()) return nullptr;

    // An instanced shader on the pre-VisibleInstanceIndices convention indexes the instance
    // buffer directly, so it cannot draw the culled subset a shadow frustum needs. Those stay on
    // the engine depth shader — exactly the behaviour they had before variants existed. The
    // warning telling the author what to add is logged once per program by the caller.
    if (materialProgram->HasInstanceDataBlock() && !materialProgram->HasVisibleIndexBlock()) return nullptr;

    TUsePointer<IShaderCode> vertexShader = materialProgram->GetVertexShader();
    if (!vertexShader) return nullptr;

    const UInt64 vertexShaderUuid = vertexShader->Uuid.getUUID();
    if (TOwningPointer<ShaderProgram>* cached = mDepthVariants.Find(vertexShaderUuid)) {
        if (!*cached || !(*cached)->IsLoaded()) return nullptr;
        return TUsePointer<ShaderProgram>(*cached);
    }

    // The material's own vertex shader linked with the engine's empty fragment shader (the same
    // one mDepthShader uses). Linking against a fragment stage that reads nothing lets the
    // compiler drop the normals/TBN/UV work, so the variant costs roughly the position math plus
    // whatever the material does to it — which is the whole point.
    EngineObjectHandle variantHandle = mApplicationInfo->AppObjectManager->CreateObject<ShaderProgram>();
    TOwningPointer<ShaderProgram> variant = mApplicationInfo->AppObjectManager->GetObjectAsOwner<ShaderProgram>(variantHandle);
    // Own uuid, derived from the vertex shader's: it keys the compiled-binary cache, so it must
    // not collide with the material program's own entry.
    variant->Uuid = PluUUID(vertexShaderUuid ^ 0x9e3779b97f4a7c15ULL);
    variant->SetVertexShader(vertexShader);
    variant->SetFragmentShader(mDepthShader->GetFragmentShader());
    variant->LoadFromBinary();

    if (!variant->IsLoaded()) {
        PLU_CORE_WARN("Renderer::ResolveDepthProgram - failed to build the depth variant of vertex shader {} — "
                      "falling back to the engine depth shader for materials using it.", vertexShaderUuid);
        mApplicationInfo->AppObjectManager->DestroyObject(variantHandle);
        // Remembered as "no variant" so the compile is not retried every frame for every batch.
        mDepthVariants.Insert(vertexShaderUuid, nullptr);
        return nullptr;
    }

    // Into the shader manager's renderable list, which is what feeds every program the frame's
    // global uniforms (time, camera, lights) and drives hot-reload recompiles. A variant created
    // mid-frame joins the list after this frame's broadcast, so its first frame runs with time 0 —
    // one frame of a wind phase, and only the first time a vertex shader is seen.
    if (DynamicArray<TUsePointer<ShaderProgram>>* renderablePrograms = mApplicationInfo->AppShaderManager->GetRenderableShaderPrograms()) {
        renderablePrograms->PushBack(TUsePointer<ShaderProgram>(variant));
    }

    PLU_CORE_INFO("Depth variant built for vertex shader {} (program uuid {})", vertexShaderUuid, variant->Uuid.getUUID());
    TUsePointer<ShaderProgram> variantUser = variant;
    mDepthVariants.Insert(vertexShaderUuid, std::move(variant));
    return variantUser;
}

void Plu::Renderer::DrawStaticDepthBatches(Plu::RenderSnapshot* snapshot, UInt32 frustumIndex,
                                           const Matrix4& view, const Matrix4& projection)
{
    const UInt32 staticBatchCount = snapshot->StaticMeshBatches.Size();
    for (UInt32 i = 0; i < staticBatchCount; i++) {
        const ShadowDrawRange& range = mShadowDrawRanges[frustumIndex * staticBatchCount + i];
        if (range.Count == 0) continue;
        const TUsePointer<StaticMesh>& staticMesh = mResolvedBatchMeshes[i];
        if (!staticMesh || !staticMesh->IsLoaded) continue;

        const StaticMeshBatch& batch = snapshot->StaticMeshBatches[i];
        TUsePointer<MaterialInfo> materialInfo = mApplicationInfo->AppAssetManager->GetAssetDataNoLoad(batch.MaterialUUID);
        TUsePointer<ShaderProgram> depthProgram = ResolveDepthProgram(materialInfo.GetRaw());
        const bool isVariant = depthProgram.IsValid();
        if (!isVariant) depthProgram = mDepthShader;
        if (!depthProgram || !depthProgram->IsLoaded()) continue;

        depthProgram->Bind();
        depthProgram->SetMatrix4Uniform("view", view);
        depthProgram->SetMatrix4Uniform("projection", projection);
        if (isVariant && materialInfo) {
            // A material parameter can drive the vertex stage (wind strength, a displacement
            // map), and the variant is its own program object with its own uniform state — so it
            // needs the material applied just like the lighting pass does.
            depthProgram->RenderFromMaterial(materialInfo.GetRaw(), mApplicationInfo->AppRenderingManager);
        }

        // A variant of a NON-instanced material draws the way the lighting pass draws it: one
        // call per instance with the transform in a uniform. Its vertex shader has no instance
        // SSBO to read, so an instanced draw would stamp the whole range at one transform.
        if (isVariant && !depthProgram->HasInstanceDataBlock()) {
            for (UInt32 v = 0; v < range.Count; v++) {
                const UInt32 instanceIndex = mVisibleInstanceScratch[range.Offset + v];
                const InstanceGPUData& instance = snapshot->StaticInstanceData[instanceIndex];
                depthProgram->SetMatrix4Uniform("model", instance.ModelMatrix);
                depthProgram->SetMatrix4Uniform("normalMatrix", instance.NormalMatrix);
                DrawStaticMesh(staticMesh.GetRaw(), mApplicationInfo->AppRenderingManager.GetRaw());
            }
            snapshot->StatDrawCalls += range.Count;
            continue;
        }

        // instanceBaseIndex indexes the VISIBLE-INDEX buffer, not the instance buffer — the extra
        // indirection is what lets one frustum draw a subset of the batch.
        depthProgram->SetIntUniform("instanceBaseIndex", static_cast<int>(range.Offset));
        DrawStaticMeshInstanced(staticMesh.GetRaw(), mApplicationInfo->AppRenderingManager.GetRaw(), range.Count);
        snapshot->StatDrawCalls++;
    }
}

void Plu::Renderer::DrawSkeletalDepthObject(Plu::RenderSnapshot* snapshot, UInt32 objectIndex,
                                            const Matrix4& view, const Matrix4& projection)
{
    SkeletalMeshRenderObject* renderObject = &snapshot->SkeletalMeshRenderObjects[objectIndex];
    const TUsePointer<SkeletalMesh>& skeletalMesh = mResolvedSkeletalMeshes[objectIndex];
    if (!skeletalMesh || !skeletalMesh->IsLoaded) return;

    TUsePointer<MaterialInfo> materialInfo = mApplicationInfo->AppAssetManager->GetAssetDataNoLoad(renderObject->MaterialUUID);
    TUsePointer<ShaderProgram> depthProgram = ResolveDepthProgram(materialInfo.GetRaw());
    const bool isVariant = depthProgram.IsValid();
    if (!isVariant) depthProgram = mSkeletalDepthShader;
    if (!depthProgram || !depthProgram->IsLoaded()) return;
    // A variant built from a material that does not skin (no bone palette in its vertex shader)
    // would put the mesh in its bind pose in the depth buffer — worse than the engine shader,
    // which at least skins. Same trap the lighting pass has with skeletal materials.
    if (isVariant && !depthProgram->HasBoneMatricesBlock()) {
        depthProgram = mSkeletalDepthShader;
        if (!depthProgram || !depthProgram->IsLoaded()) return;
    }

    depthProgram->Bind();
    depthProgram->SetMatrix4Uniform("view", view);
    depthProgram->SetMatrix4Uniform("projection", projection);
    if (isVariant && materialInfo) {
        depthProgram->RenderFromMaterial(materialInfo.GetRaw(), mApplicationInfo->AppRenderingManager);
    }
    depthProgram->SetIntUniform("paletteBaseIndex", static_cast<int>(mSkeletalPaletteRanges[objectIndex].Offset));
    depthProgram->SetMatrix4Uniform("model", renderObject->ModelMatrix);
    DrawSkeletalMesh(skeletalMesh.GetRaw(), mApplicationInfo->AppRenderingManager.GetRaw());
    snapshot->StatDrawCalls++;
}

void Plu::Renderer::DestroyDepthVariants()
{
    DynamicArray<TUsePointer<ShaderProgram>>* renderablePrograms = mApplicationInfo->AppShaderManager->GetRenderableShaderPrograms();
    for (auto& entry : mDepthVariants) {
        TOwningPointer<ShaderProgram>& variant = entry.second;
        if (!variant) continue;
        // Out of the renderable list first: the per-frame loop dereferences every entry, and a
        // TUsePointer to a destroyed object throws rather than reading as null.
        if (renderablePrograms) {
            for (UInt32 i = renderablePrograms->Size(); i > 0; --i) {
                if (renderablePrograms->At(i - 1).GetRaw() == variant.GetRaw()) {
                    renderablePrograms->RemoveAt(i - 1);
                }
            }
        }
        variant->UnloadProgram();
        mApplicationInfo->AppObjectManager->DestroyObject(variant->GetObjectHandle());
    }
    mDepthVariants.Clear();
}

void Plu::Renderer::PrepareShadowCascades(Plu::RenderSnapshot *snapshot, const Matrix4& cameraView)
{
    PLU_PROFILE_SCOPE("Renderer::PrepareShadowCascades");
    mCascades.Clear();

    const DirectionalLightShadowSettings settings = ClampShadowSettings(snapshot->DirLight.Shadow);
    mShadowSettings = settings;

    if (!snapshot->HasDirLight || !settings.CastShadows) {
        // No directional shadows this frame. Clear the atlas ONCE on the transition so leftover
        // content (e.g. shadows baked during a previous PIE session) doesn't linger, then stop
        // touching it — with CascadeCount = 0 nothing samples it anyway.
        if (!mShadowMapsCleared) {
            if (mShadowAtlasFrameBuffer) {
                mShadowAtlasFrameBuffer->Clear(0.0f, 0.0f, 0.0f, 1.0f);
            }
            mShadowMapsCleared = true;
        }
        return;
    }

    // Resolution / cascade count / falloff are settings, so the GL resources may need rebuilding.
    // Safe here: the render thread owns the GL context, and nothing samples the atlas until the
    // main pass below.
    BuildCascadeAtlas(settings);
    if (!mShadowAtlas) {
        return;
    }
    mShadowMapsCleared = false;

    // No static depth shader, no depth pass — leaving mCascades empty makes
    // UpdateShadowDataBuffer publish CascadeCount = 0, so nothing samples a map we never wrote.
    if (!mDepthShader || !mDepthShader->IsLoaded()) return;

    TUsePointer<IWindow> window = mApplicationInfo->AppWindow;
    const float aspect = static_cast<float>(window->GetWidth()) / static_cast<float>(window->GetHeight());
    const float fovRad = glm::radians(snapshot->CameraFOV);

    // A higher lambda packs the near cascades tighter against the camera — sharper close-up
    // shadows. At the defaults (4 cascades, 150 m, lambda 0.9) the splits land around
    // 4.3 / 11 / 33 / 150 m; at lambda 0.95 they move in to 2.5 / 7.4 / 28.5 / 150 m.
    CascadeConfig cascadeConfig;
    cascadeConfig.CascadeCount   = settings.CascadeCount;
    cascadeConfig.ShadowDistance = settings.ShadowDistance;
    cascadeConfig.SplitLambda    = settings.SplitLambda;
    cascadeConfig.Resolution     = settings.Resolution;
    cascadeConfig.ResolutionFalloff = settings.ResolutionFalloff;

    ComputeCascadeSplits(cascadeConfig, kCameraNearClip, mCascadeSplits);
    // The resolutions passed here are the SAME ones the atlas was just laid out with, which is
    // what keeps the texel snap on the grid the cascade actually renders at.
    ComputeCascadeMatrices(
        cameraView, fovRad, aspect,
        kCameraNearClip,
        snapshot->DirLight.Direction,
        cascadeConfig,
        mCascadeSplits,
        mCascades,
        mCascadeResolutions.Data()
    );
}

void Plu::Renderer::EnsureDepthPrepassBuffer()
{
    if (!mMainBuffer) return;

    const Int32 width  = mMainBuffer->GetWidth();
    const Int32 height = mMainBuffer->GetHeight();
    if (width <= 0 || height <= 0) return;

    if (mDepthPrepassBuffer && mDepthPrepassBuffer->GetWidth() == width
        && mDepthPrepassBuffer->GetHeight() == height) {
        return;
    }

    // Rebuild rather than Resize: this only happens when the window changes size, and a fresh
    // framebuffer cannot end up half-migrated the way an in-place resize of a texture attachment
    // can. The observer goes first — the texture belongs to the framebuffer being destroyed.
    mSceneDepthTexture = nullptr;
    if (mDepthPrepassBuffer) {
        mApplicationInfo->AppObjectManager->DestroyObject(mDepthPrepassBuffer->GetObjectHandle());
        mDepthPrepassBuffer->Destroy();
        mDepthPrepassBuffer = nullptr;
    }

    EngineObjectHandle handle = mApplicationInfo->AppObjectManager->CreateObject<FrameBuffer>();
    mDepthPrepassBuffer = mApplicationInfo->AppObjectManager->GetObjectAsOwner<FrameBuffer>(handle);
    // DepthOnly gives a D32F texture. Nothing has to match the main buffer's D24S8 renderbuffer
    // any more (see RenderDepthPrepass on why the depth is not blitted), and the extra precision
    // goes straight into the contact-shadow ray, which linearises this value per sample.
    if (!mDepthPrepassBuffer->Create(width, height, mApplicationInfo->AppObjectManager, FrameBufferType::DepthOnly)
        || !CheckShadowGLError("Renderer::EnsureDepthPrepassBuffer")) {
        PLU_CORE_ERROR("Renderer::EnsureDepthPrepassBuffer - Failed to create the depth prepass framebuffer ({}x{}) — contact shadows and early-Z disabled",
                       width, height);
        mApplicationInfo->AppObjectManager->DestroyObject(mDepthPrepassBuffer->GetObjectHandle());
        mDepthPrepassBuffer->Destroy();
        mDepthPrepassBuffer = nullptr;
        return;
    }

    mSceneDepthTexture = mDepthPrepassBuffer->GetDepthTexture();
    PLU_CORE_INFO("Depth prepass buffer ready: {}x{} D32F", width, height);
}

bool Plu::Renderer::AreContactShadowsActive(const Plu::RenderSnapshot* snapshot) const
{
    return snapshot->HasDirLight
        && mShadowSettings.CastShadows
        && mShadowSettings.ContactShadows
        && mShadowSettings.ContactShadowLength > 0.0f;
}

void Plu::Renderer::UnbindSceneDepthTexture()
{
    glActiveTexture(GL_TEXTURE0 + kSceneDepthTextureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Plu::Renderer::RenderDepthPrepass(Plu::RenderSnapshot* snapshot, const Matrix4& view, const Matrix4& projection)
{
    if (!mDepthPrepassBuffer || !mDepthShader || !mDepthShader->IsLoaded()) return;
    // Nothing samples the scene depth this frame, so the whole geometry pass would be waste.
    // Contact shadows are its only consumer today — add to AreContactShadowsActive when that
    // stops being true (SSAO, SSR, ...), or the new consumer will read a stale buffer.
    if (!AreContactShadowsActive(snapshot)) return;

    PLU_PROFILE_SCOPE("Renderer::RenderDepthPrepass");
    PLU_PROFILE_SCOPE_GPU("Renderer::DepthPrepass");

    // Same rule as the shadow atlas: the texture about to become the render target must not still
    // be bound for sampling from the previous frame's lighting pass.
    UnbindSceneDepthTexture();

    mDepthPrepassBuffer->Clear(0.0f, 0.0f, 0.0f, 1.0f);
    mDepthPrepassBuffer->Bind();

    // Static meshes — each batch through its material's depth variant when it has one, so a
    // vertex-animated material (wind) writes the depth of the geometry actually drawn on screen.
    DrawStaticDepthBatches(snapshot, CameraFrustumIndex(), view, projection);

    // Skeletal meshes — skinned depth, so animated geometry occludes contact-shadow rays exactly
    // where it is drawn.
    if (mSkeletalDepthReady) {
        const UInt64 skeletalMeshCount = snapshot->SkeletalMeshRenderObjects.Size();
        for (UInt32 i = 0; i < skeletalMeshCount; i++) {
            // No frustum test — the lighting pass draws every skeletal mesh unconditionally, and
            // the prepass must not hold anything it does not draw (see CullShadowCasters). Non-
            // casters are skipped: CastsShadow turns off their contact shadows as well.
            if (!snapshot->SkeletalMeshRenderObjects[i].CastsShadow) continue;
            DrawSkeletalDepthObject(snapshot, i, view, projection);
        }
    }

    mDepthPrepassBuffer->Unbind();

    // Still NO blit into the main buffer, and therefore no early-Z — but the obstacle is now one
    // step away rather than structural. This pass draws with the materials' own vertex shaders
    // (ResolveDepthProgram), and the engine depth shaders take `view` and `projection` separately
    // instead of a premultiplied lightSpaceMatrix, so both passes build gl_Position from the same
    // expression in the same order. What remains before this depth can be trusted by a GL_LEQUAL
    // lighting pass is `invariant gl_Position` in the shaders (two programs compiled from one
    // source may still differ by an ulp without it) and sharing the depth attachment with the main
    // framebuffer. Until then the prepass exists to feed contact shadows.
    CheckShadowGLError("Renderer::RenderDepthPrepass");
}

void Plu::Renderer::RenderShadowPass(Plu::RenderSnapshot *snapshot)
{
    if (mCascades.IsEmpty() || !mShadowAtlasFrameBuffer) return;

    PLU_PROFILE_SCOPE("Renderer::RenderShadowPass");
    PLU_PROFILE_SCOPE_GPU("Renderer::RenderShadowPass");

    const UInt64 skeletalMeshCount = snapshot->SkeletalMeshRenderObjects.Size();

    // Front-face culling tylko na czas map cieni: do bufora głębi trafiają TYLNE ściany
    // obiektów, więc próg self-shadowingu (acne) przesuwa się na niewidoczną, odwróconą od
    // kamery stronę geometrii — najskuteczniejszy zabieg na acne płaskich/prostopadłych
    // powierzchni. Culling jest globalnie wyłączony (główny pass renderuje obie strony),
    // więc po passie przywracamy stan. Uwaga: dla otwartej/jednostronnej geometrii (pojedyncze
    // quady) może dać light-leak — wtedy normal-offset w PBR.frag łagodzi przypadki brzegowe.
    // Mesh index buffers are clockwise (the importers use aiProcess_FlipWindingOrder), so GL's
    // default GL_CCW front face would make GL_FRONT cull the real back faces — the exact opposite.
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
    glCullFace(GL_FRONT);

    // No polygon offset on triangles. With working front-face culling only faces turned away
    // from the light reach the map, and PBR.frag samples the shadow only where NdotL > 0, so a
    // lit surface never compares against its own depth — there is no acne for the offset to
    // cure. What it did do was push those back faces further from the light: at the foot of an
    // object the back face sits right on the ground, and a slope-scaled offset on a steep wall
    // turned straight into a gap between the object and its shadow (peter-panning).
    // The offset is kept for particle points below, which culling never touches.
    constexpr float kShadowPolygonOffsetFactor = 2.0f;
    constexpr float kShadowPolygonOffsetUnits  = 4.0f;
    glPolygonOffset(kShadowPolygonOffsetFactor, kShadowPolygonOffsetUnits);

    // The array is about to become the render target, so it must not still be bound for
    // sampling from the previous frame's main pass — see UnbindShadowTexture.
    UnbindShadowTexture();

    // Depth clamping ("pancaking"): casters between the light and the cascade sphere are in
    // front of the ortho near plane. Instead of pushing that plane 50 m towards the light —
    // which stretches the depth range of every cascade and costs precision everywhere — we
    // clamp them onto the near plane. Their exact depth is wrong, but they are nearer than
    // anything in the cascade anyway, so the comparison result is not.
    glEnable(GL_DEPTH_CLAMP);

    const UInt32 staticBatchCount = snapshot->StaticMeshBatches.Size();

    // One bind and one clear for the whole atlas: the cascades are regions of a single depth
    // texture now, so clearing them individually would need a scissor rect per cascade to cover
    // exactly the same texels this one call does.
    mShadowAtlasFrameBuffer->Clear(0.0f, 0.0f, 0.0f, 1.0f);
    mShadowAtlasFrameBuffer->Bind();  // sets glViewport to the full atlas; overridden per cascade below

    // Particle casters are points sized in the vertex shader. Wide points are NOT clipped to the
    // viewport — a point near a cascade's edge would write depth into the cascade packed next to
    // it — so each cascade also gets a scissor of its own rect. Polygon offset does not reach
    // points unless GL_POLYGON_OFFSET_POINT is on; it gives them the same caster-side bias.
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_POLYGON_OFFSET_POINT);
    glEnable(GL_SCISSOR_TEST);

    for (UInt32 c = 0; c < mCascades.Size(); c++) {
        PLU_PROFILE_SCOPE_GPU(CascadeGpuScopeNames()[c]);

        // The cascade's square in the atlas. This is the ONLY thing separating one cascade's
        // depth from another's — there is no per-cascade attachment any more.
        const ShadowAtlasRect& rect = mCascadeAtlasRects[c];
        glViewport(rect.X, rect.Y, rect.Size, rect.Size);
        glScissor(rect.X, rect.Y, rect.Size, rect.Size);

        // Static meshe — instanced shader głębi, batche z RenderSnapshotBuilder::BatchStaticMeshes.
        // Kamerowy culling z batchowania (VisibleCount) jest dla cieni bezużyteczny — caster poza
        // kadrem kamery może rzucać cień w kadr — więc pass cieni cullinguje sam, per kaskada, i
        // adresuje instancje przez skompaktowaną tablicę indeksów. Jeden glDrawElementsInstanced
        // na batch, a batch niewidoczny w tej kaskadzie odpada bez draw calla.
        DrawStaticDepthBatches(snapshot, c, mCascades[c].View, mCascades[c].Proj);

        // Skeletal meshe — skinowany shader głębi z tą samą paletą kości co główny pass, dzięki
        // czemu cień podąża za animacją. Palety WSZYSTKICH meshy poszły na GPU raz na klatkę
        // (UploadSkeletalPalettes), więc tutaj zostaje tylko offset w tym buforze — dawniej każdy
        // obiekt nadpisywał wspólny bufor, per kaskada, czyli 5x ten sam upload co klatkę.
        if (mSkeletalDepthReady) {
            const Frustum cascadeFrustum = ExtractFrustumPlanes(mCascades[c].ViewProj);
            for (UInt32 i = 0; i < skeletalMeshCount; i++) {
                SkeletalMeshRenderObject* renderObject = &snapshot->SkeletalMeshRenderObjects[i];
                if (!renderObject->CastsShadow) continue;
                if (!SphereInFrustumNoNear(cascadeFrustum, renderObject->BoundsCenter, renderObject->BoundsRadius)) {
                    snapshot->StatCulledCount++;
                    continue;
                }

                DrawSkeletalDepthObject(snapshot, i, mCascades[c].View, mCascades[c].Proj);
                mCascadeCasterCounts[c]++;
            }
        }

        DrawParticleShadowCasters(snapshot, mCascades[c].ViewProj, mCascades[c].Proj, rect.Size);

        CheckShadowGLError("Renderer::RenderShadowPass (cascade draw)");
    }

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_POLYGON_OFFSET_POINT);
    glDisable(GL_PROGRAM_POINT_SIZE);

    mShadowAtlasFrameBuffer->Unbind();

    // Przywróć stan cullingu, polygon offsetu i depth clampa do domyślnego dla głównego passa.
    glDisable(GL_DEPTH_CLAMP);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0.0f, 0.0f);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
}

UInt32 Plu::Renderer::CullShadowCasters(Plu::RenderSnapshot* snapshot)
{
    PLU_PROFILE_SCOPE("Renderer::CullShadowCasters");

    const UInt32 cascadeCount  = mCascades.Size();
    const UInt32 spotSlotCount = mSpotShadowMatrices.Size();
    // +1 for the camera frustum, which the depth prepass draws from. Appending it here rather
    // than culling separately keeps the whole frame at ONE visible-index upload on ONE binding.
    const UInt32 frustumCount  = cascadeCount + spotSlotCount + 1;
    const UInt32 batchCount    = snapshot->StaticMeshBatches.Size();

    mVisibleInstanceScratch.Clear();
    mShadowDrawRanges.Clear();
    mShadowDrawRanges.Resize(frustumCount * batchCount);
    // Cleared and resized HERE, so a frame that produced no cascades / no spot slots publishes
    // zeroed stats instead of last frame's numbers.
    mCascadeCasterCounts.Clear();
    mCascadeCasterCounts.Resize(cascadeCount);
    mSpotShadowCasterCounts.Clear();
    mSpotShadowCasterCounts.Resize(spotSlotCount);

    UInt32 culledCount = 0;

    const UInt32 cameraFrustum = CameraFrustumIndex();

    for (UInt32 f = 0; f < frustumCount; f++) {
        const bool isCascade = f < cascadeCount;
        const bool isCamera  = f == cameraFrustum;
        // The camera entry needs no frustum of its own — see the isCamera branch below.
        const Frustum shadowFrustum = isCamera
            ? Frustum{}
            : ExtractFrustumPlanes(isCascade ? mCascades[f].ViewProj
                                             : mSpotShadowMatrices[f - cascadeCount]);

        for (UInt32 b = 0; b < batchCount; b++) {
            const StaticMeshBatch& batch = snapshot->StaticMeshBatches[b];
            ShadowDrawRange& range = mShadowDrawRanges[f * batchCount + b];
            range.Offset = static_cast<UInt32>(mVisibleInstanceScratch.Size());
            range.Count  = 0;

            if (batch.TotalCount == 0) continue;

            if (isCamera) {
                // CastsShadow gates contact shadows too: the prepass depth is what their rays
                // march through, so a non-caster left out of it neither shadows its surroundings
                // nor itself. Leaving geometry OUT is safe — the prepass feeds no early-Z, and
                // every ray starts from the shaded fragment's own position, not from this buffer.
                if (!batch.CastsShadow) continue;
                // The camera's range is taken VERBATIM from the batch — the instances MAIN already
                // marked visible — instead of being re-culled here: re-deriving the frustum on
                // this thread could disagree by an ulp, and an instance present in the depth
                // buffer but absent from the colour pass would cast contact shadows from nothing.
                for (UInt32 v = 0; v < batch.VisibleCount; v++) {
                    mVisibleInstanceScratch.PushBack(batch.InstanceOffset + v);
                }
                range.Count = batch.VisibleCount;
                continue;
            }

            if (!batch.CastsShadow) continue;

            const UInt32 end = batch.InstanceOffset + batch.TotalCount;
            for (UInt32 instance = batch.InstanceOffset; instance < end; instance++) {
                const InstanceCullData& bounds = snapshot->StaticInstanceBounds[instance];
                // Cascades skip the near plane because depth-clamped casters legitimately sit in
                // front of it (see SphereInFrustumNoNear) — culling them would remove the very
                // objects casting into the cascade. A spot slot has no depth clamp (pancaking a
                // perspective projection would invent shadows right at the apex), so its near
                // plane is real and must be tested. The camera's near plane is as real as it gets.
                const bool visible = isCascade
                    ? SphereInFrustumNoNear(shadowFrustum, bounds.BoundsCenter, bounds.BoundsRadius)
                    : SphereInFrustum(shadowFrustum, bounds.BoundsCenter, bounds.BoundsRadius);
                if (!visible) {
                    // The camera's culling is already counted on MAIN (RenderSnapshotBuilder), so
                    // counting it again here would report every off-screen object twice.
                    if (!isCamera) culledCount++;
                    continue;
                }
                mVisibleInstanceScratch.PushBack(instance);
                range.Count++;
            }
            if (isCamera) {
                // No per-frustum stat for the camera — the prepass is not a shadow map.
            } else if (isCascade) {
                mCascadeCasterCounts[f] += range.Count;
            } else {
                mSpotShadowCasterCounts[f - cascadeCount] += range.Count;
            }
        }
    }

    if (!mVisibleInstanceScratch.IsEmpty()) {
        const Int32 needed = static_cast<Int32>(mVisibleInstanceScratch.Size());
        // Grow with 2x headroom, like the instance buffer: a scene whose visible-caster count
        // wobbles frame to frame would otherwise reallocate every frame.
        if (mVisibleInstanceBuffer.GetCount() < needed) {
            mVisibleInstanceBuffer.Resize(needed * 2);
        }
        mVisibleInstanceBuffer.Update(mVisibleInstanceScratch.Data(), needed);
    }
    // Bind after any Resize — Resize creates a new buffer ID and the indexed binding point would
    // otherwise still hold the deleted one.
    mVisibleInstanceBuffer.BindBase(3);

    return culledCount;
}

void Plu::Renderer::PrepareSpotShadowSlots(Plu::RenderSnapshot* snapshot)
{
    PLU_PROFILE_SCOPE("Renderer::PrepareSpotShadowSlots");

    mSpotShadowSlotOwners.Clear();
    mSpotShadowMatrices.Clear();
    mSpotShadowViews.Clear();
    mSpotShadowProjs.Clear();

    if (snapshot->SpotLights.IsEmpty()) return;
    if (!mDepthShader || !mDepthShader->IsLoaded()) return;

    RecreateSpotShadowResources();
    if (mSpotShadowFrameBuffers.IsEmpty()) return;

    // The snapshot arrives sorted descending by importance (RenderSnapshotBuilder), so handing
    // out slots is just walking the front of the list — no sorting on the render thread.
    //
    // Every slot is redrawn from scratch each frame, so a light swapping slots between frames is
    // invisible. What IS visible is a light crossing the budget boundary: its shadow appears or
    // disappears. That is inherent to a fixed pool, and SpotLight::ShadowPriority exists so a
    // scene can pin the shadows it actually cares about above the competition.
    const UInt32 lightCount = snapshot->SpotLights.Size();
    const UInt32 slotBudget = static_cast<UInt32>(kMaxSpotShadowSlots);
    for (UInt32 i = 0; i < lightCount && mSpotShadowSlotOwners.Size() < slotBudget; i++) {
        const SpotLightRenderObject& light = snapshot->SpotLights[i];
        if (!light.CastShadows) continue;

        mSpotShadowSlotOwners.PushBack(static_cast<Int32>(i));
        Matrix4 slotView, slotProj;
        mSpotShadowMatrices.PushBack(ComputeSpotLightMatrix(light.Location, light.Direction, light.Range,
                                                            light.OuterConeAngle, slotView, slotProj));
        mSpotShadowViews.PushBack(slotView);
        mSpotShadowProjs.PushBack(slotProj);
    }
}

void Plu::Renderer::RenderSpotShadowPass(Plu::RenderSnapshot* snapshot)
{
    if (mSpotShadowSlotOwners.IsEmpty()) return;

    PLU_PROFILE_SCOPE("Renderer::RenderSpotShadowPass");
    PLU_PROFILE_SCOPE_GPU("Renderer::RenderSpotShadowPass");

    // The atlas is about to become the render target, so it must not still be bound for sampling
    // from the previous frame's main pass — same feedback loop as the cascade array.
    UnbindSpotShadowTexture();

    // Same caster-side setup as the cascades: front-face culling moves the acne threshold onto
    // the geometry's hidden side, clockwise front face because mesh index buffers are stored
    // clockwise, and no polygon offset on triangles (see RenderShadowPass) — only on points.
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
    glCullFace(GL_FRONT);
    constexpr float kShadowPolygonOffsetFactor = 2.0f;
    constexpr float kShadowPolygonOffsetUnits  = 4.0f;
    glPolygonOffset(kShadowPolygonOffsetFactor, kShadowPolygonOffsetUnits);

    // Particle casters, as in the cascade pass. No scissor needed: every slot is its own layer.
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_POLYGON_OFFSET_POINT);

    // Deliberately NO GL_DEPTH_CLAMP here, unlike the cascade pass. Pancaking works for an ortho
    // projection; under a perspective one it would flatten every caster in front of the near
    // plane onto it, inventing a shadow right at the cone apex. kSpotShadowNearClip (5 cm) sits
    // close enough to the apex that nothing real is lost by testing the near plane honestly.

    const UInt32 staticBatchCount = snapshot->StaticMeshBatches.Size();
    const UInt32 cascadeCount     = mCascades.Size();
    const UInt32 skeletalMeshCount = snapshot->SkeletalMeshRenderObjects.Size();

    for (UInt32 s = 0; s < mSpotShadowSlotOwners.Size(); s++) {
        PLU_PROFILE_SCOPE_GPU(SpotShadowGpuScopeNames()[s]);

        mSpotShadowFrameBuffers[s]->Clear(0.0f, 0.0f, 0.0f, 1.0f);
        mSpotShadowFrameBuffers[s]->Bind(); // sets the viewport to the slot resolution

        const Matrix4& lightMatrix = mSpotShadowMatrices[s];

        // Static meshes — the spot frusta were culled in the same sweep as the cascades
        // (CullShadowCasters), so their ranges sit right after the cascades' in mShadowDrawRanges.
        DrawStaticDepthBatches(snapshot, cascadeCount + s, mSpotShadowViews[s], mSpotShadowProjs[s]);

        if (mSkeletalDepthReady) {
            // Full frustum test including the near plane, for the no-depth-clamp reason above.
            const Frustum spotFrustum = ExtractFrustumPlanes(lightMatrix);
            for (UInt32 i = 0; i < skeletalMeshCount; i++) {
                SkeletalMeshRenderObject* renderObject = &snapshot->SkeletalMeshRenderObjects[i];
                if (!renderObject->CastsShadow) continue;
                if (!SphereInFrustum(spotFrustum, renderObject->BoundsCenter, renderObject->BoundsRadius)) {
                    snapshot->StatCulledCount++;
                    continue;
                }

                DrawSkeletalDepthObject(snapshot, i, mSpotShadowViews[s], mSpotShadowProjs[s]);
                mSpotShadowCasterCounts[s]++;
            }
        }

        DrawParticleShadowCasters(snapshot, lightMatrix, mSpotShadowProjs[s], mSpotShadowResolution);

        mSpotShadowFrameBuffers[s]->Unbind();
        CheckShadowGLError("Renderer::RenderSpotShadowPass (slot draw)");
    }

    glDisable(GL_POLYGON_OFFSET_POINT);
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0.0f, 0.0f);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
}

void Plu::Renderer::UpdateSpotLightBuffers(Plu::RenderSnapshot* snapshot)
{
    PLU_PROFILE_SCOPE("Renderer::UpdateSpotLightBuffers");

    mSpotLightScratch.Clear();
    mSpotLightIndices.Clear();
    mSpotLightData = SpotLightDataGPU();

    // Defensive clamp: MAIN already trims to kMaxVisibleSpotLights, and the GPU buffers are
    // allocated at exactly that size, so anything beyond it would overrun the upload.
    const UInt32 snapshotLightCount = static_cast<UInt32>(snapshot->SpotLights.Size());
    const UInt32 lightCount = snapshotLightCount < static_cast<UInt32>(kMaxVisibleSpotLights)
                            ? snapshotLightCount
                            : static_cast<UInt32>(kMaxVisibleSpotLights);
    mSpotLightScratch.Reserve(lightCount);
    mSpotLightIndices.Reserve(lightCount);

    for (UInt32 i = 0; i < lightCount; i++) {
        const SpotLightRenderObject& light = snapshot->SpotLights[i];

        SpotLightGPU gpu{};
        // Identity, not garbage: a light without a slot never has its matrix read (the shader
        // checks shadowSlot first), but leaving it uninitialised makes any future bug read as a
        // wild transform instead of an obvious no-op.
        gpu.ShadowViewProj = Matrix4(1.0f);
        gpu.Position       = light.Location;
        gpu.Range          = light.Range;
        gpu.Direction      = light.Direction;
        gpu.InnerConeCos   = light.InnerConeCos;
        // Colour premultiplied by intensity on the CPU — the shader has no use for them apart.
        gpu.Color          = light.Color * light.Intensity;
        gpu.OuterConeCos   = light.OuterConeCos;
        gpu.ShadowSlot     = -1;
        gpu.ShadowDepthBias  = light.ShadowDepthBias;
        gpu.ShadowNormalBias = light.ShadowNormalBias;
        gpu.ShadowPcfRadius  = light.ShadowPcfRadius;
        // World size of one texel per metre of distance: the projection spans
        // 2*tan(outerHalfAngle) world units at 1 m, divided across the slot's resolution.
        gpu.ShadowTexelWorldPerMetre = mSpotShadowResolution > 0
                                     ? (2.0f * std::tan(light.OuterConeAngle)) / static_cast<float>(mSpotShadowResolution)
                                     : 0.0f;
        gpu.ShadowPcfTaps  = glm::clamp(light.ShadowPcfTaps, 1, kMaxShadowPcfTaps);
        // f*n/(f-n) of THIS light's shadow projection — the constant part of dz01/dd, which is
        // what turns a bias authored in metres into this frame's projected depth. Must use the
        // same near/far as ComputeSpotLightMatrix, hence the identical clamp on the far plane.
        const float farPlane = std::max(light.Range, kSpotShadowNearClip + 0.01f);
        gpu.ShadowDepthBiasScale = (farPlane * kSpotShadowNearClip) / (farPlane - kSpotShadowNearClip);

        mSpotLightScratch.PushBack(gpu);
    }

    // Slots second, so a light that lost the competition keeps ShadowSlot = -1 from above.
    for (UInt32 s = 0; s < mSpotShadowSlotOwners.Size(); s++) {
        const UInt32 lightIndex = static_cast<UInt32>(mSpotShadowSlotOwners[s]);
        if (lightIndex >= mSpotLightScratch.Size()) continue;
        mSpotLightScratch[lightIndex].ShadowSlot     = static_cast<Int32>(s);
        mSpotLightScratch[lightIndex].ShadowViewProj = mSpotShadowMatrices[s];
    }

    // One global index list — the whole clustered-forward investment. Today it is [0, count) and
    // the offset is 0; with clusters it becomes per-cluster sub-lists and PBR.frag does not change.
    for (UInt32 i = 0; i < lightCount; i++) {
        mSpotLightIndices.PushBack(i);
    }

    mSpotLightData.SpotLightOffset = 0;
    mSpotLightData.SpotLightCount  = static_cast<Int32>(lightCount);
    mSpotLightData.InvSpotShadowResolution = mSpotShadowResolution > 0
                                           ? 1.0f / static_cast<float>(mSpotShadowResolution)
                                           : 0.0f;
    mSpotLightData.SpotShadowSlotCount = static_cast<Int32>(mSpotShadowSlotOwners.Size());

    // Uploaded unconditionally, every frame — with SpotLightCount = 0 when there is nothing to
    // light. Same rule as ShadowData: there is no frame in which a shader reads the previous
    // frame's light list.
    if (lightCount > 0) {
        mSpotLightBuffer.Update(mSpotLightScratch.Data(), static_cast<Int32>(lightCount));
        mSpotLightIndexBuffer.Update(mSpotLightIndices.Data(), static_cast<Int32>(lightCount));
    }
    mSpotLightDataBuffer.Update(mSpotLightData);
}

void Plu::Renderer::UpdateShadowDataBuffer(Plu::RenderSnapshot* snapshot)
{
    PLU_PROFILE_SCOPE("Renderer::UpdateShadowDataBuffer");

    mShadowData = ShadowDataGPU();
    mShadowData.CascadeCount = static_cast<Int32>(mCascades.Size());

    const float atlasWidth  = static_cast<float>(std::max(mShadowAtlasWidth, 1));
    const float atlasHeight = static_cast<float>(std::max(mShadowAtlasHeight, 1));

    for (UInt32 c = 0; c < mCascades.Size() && c < static_cast<UInt32>(kMaxShadowCascades); c++) {
        const ShadowCascadeData& cascade = mCascades[c];
        ShadowCascadeGPU& gpu = mShadowData.Cascades[c];

        gpu.ViewProj = cascade.ViewProj;

        // Where this cascade lives in the atlas, as the scale/bias the shader applies to its
        // [0,1] projected coordinates. Computed here rather than in GLSL because it is per
        // cascade, not per fragment — and because it is the only thing that has to agree with
        // the glViewport the depth pass used.
        const ShadowAtlasRect& rect = mCascadeAtlasRects[c];
        gpu.AtlasScaleBias = Vec4(
            static_cast<float>(rect.Size) / atlasWidth,
            static_cast<float>(rect.Size) / atlasHeight,
            static_cast<float>(rect.X)    / atlasWidth,
            static_cast<float>(rect.Y)    / atlasHeight);

        gpu.Params.x = cascade.SplitDistance;
        gpu.Params.y = cascade.TexelWorldSize;
        // The depth bias is authored in METRES; converting it into each cascade's own [0,1]
        // depth range here is what makes "5 mm of bias" mean 5 mm in every cascade. Doing it on
        // the CPU also retires the per-fragment GLSL helper that used to recover the same scale
        // from the light matrix.
        gpu.Params.z = cascade.DepthRange > 0.0f
                     ? mShadowSettings.DepthBias / cascade.DepthRange
                     : 0.0f;
        gpu.Params.w = 0.0f;
    }

    // Fade out over the last quarter of the shadow distance instead of cutting off at the end
    // of the last cascade.
    mShadowData.ShadowFadeEnd        = mShadowSettings.ShadowDistance;
    mShadowData.ShadowFadeStart      = mShadowSettings.ShadowDistance * 0.85f;
    mShadowData.CascadeBlendFraction = mShadowSettings.CascadeBlend;
    mShadowData.NormalBiasScale      = mShadowSettings.NormalBias;
    mShadowData.PcfRadiusTexels      = mShadowSettings.PcfRadius;
    mShadowData.PcfTapCount          = mShadowSettings.PcfTapCount;
    mShadowData.PcfRotateSamples     = mShadowSettings.PcfRotate ? 1 : 0;
    mShadowData.DebugVisualizeCascades = snapshot->ShowShadowCascades ? 1 : 0;
    // One texel of the atlas is one texel of whichever cascade owns it, so a single inverse size
    // converts the PCF radius (authored in texels) to UV for every cascade — no per-cascade
    // resolution needed in the shader even though they now differ.
    mShadowData.InvAtlasSize = Vec2(1.0f / atlasWidth, 1.0f / atlasHeight);

    // Contact shadows. Steps = 0 is the single "off" switch the shader tests, so everything that
    // can disable them — the setting, a missing prepass texture, a degenerate length — collapses
    // into it here rather than being re-checked per fragment.
    const bool contactShadowsUsable = AreContactShadowsActive(snapshot) && mSceneDepthTexture;
    mShadowData.ContactShadowSteps     = contactShadowsUsable ? mShadowSettings.ContactShadowSteps : 0;
    mShadowData.ContactShadowLength    = mShadowSettings.ContactShadowLength;
    mShadowData.ContactShadowThickness = mShadowSettings.ContactShadowThickness;
    mShadowData.ContactShadowBias      = mShadowSettings.ContactShadowBias;

    mShadowDataBuffer.Update(mShadowData);
}

void Plu::Renderer::BuildSkeletalPalettes(Plu::RenderSnapshot* snapshot)
{
    mSkeletalPaletteScratch.Clear();
    mSkeletalPaletteRanges.Clear();
    const UInt32 skeletalMeshCount = static_cast<UInt32>(snapshot->SkeletalMeshRenderObjects.Size());
    mSkeletalPaletteRanges.Reserve(skeletalMeshCount);
    for (UInt32 i = 0; i < skeletalMeshCount; i++) {
        const SkeletalMeshRenderObject& renderObject = snapshot->SkeletalMeshRenderObjects[i];
        SkeletalPaletteRange range;
        range.Offset = static_cast<UInt32>(mSkeletalPaletteScratch.Size());
        for (const auto& bone : renderObject.Bones) {
            // {offset, global}: skin = global * offset. The reverse also yields identity in
            // bind pose (offset == global⁻¹), so a swap here only breaks animated poses.
            mSkeletalPaletteScratch.PushBack(bone.second * bone.first);
        }
        range.Count = static_cast<UInt32>(mSkeletalPaletteScratch.Size()) - range.Offset;
        mSkeletalPaletteRanges.PushBack(range);
    }
}

void Plu::Renderer::UploadSkeletalPalettes()
{
    PLU_PROFILE_SCOPE_GPU("Renderer::SkeletalPaletteUpload");

    const Int32 needed = static_cast<Int32>(mSkeletalPaletteScratch.Size());
    if (needed > 0) {
        // Grow with 2x headroom, like the instance buffer.
        if (mSkeletalMatricesBuffer.GetCount() < needed) {
            mSkeletalMatricesBuffer.Resize(needed * 2);
        }
        mSkeletalMatricesBuffer.Update(mSkeletalPaletteScratch.Data(), needed);
    }
    // BindBase AFTER any Resize — Resize creates a new buffer ID and the indexed binding point
    // would otherwise still hold the deleted buffer.
    mSkeletalMatricesBuffer.BindBase(0);
}

void Plu::Renderer::RenderSnapshot(Plu::RenderSnapshot *snapshot, float deltaTime)
{
    PLU_PROFILE_SCOPE("Renderer::RenderSnapshot");
    if (!snapshot->IsSnapshotValid) return;

    //Exposed to shaders for sin and that kind of stuff
    static float shaderTime = 0.0f;
    shaderTime += deltaTime;

    // Backend ImGui (koniec poprzedniej klatki) bindował programy surowym glUseProgram —
    // cache deduplikacji Bind() startuje klatkę jako "nieznany".
    ShaderProgram::ResetBindCache();

    // Palety skinningu wszystkich skeletal meshy liczone RAZ i wysyłane na GPU RAZ; pass cieni
    // (per kaskada) i pass główny adresują swoje zakresy uniformem "paletteBaseIndex".
    BuildSkeletalPalettes(snapshot);
    UploadSkeletalPalettes();

    // Wskaźniki meshy rozwiązane RAZ na klatkę — konsumują je pass cieni (per kaskada)
    // i pass główny (patrz komentarz przy mResolvedBatchMeshes w Renderer.h).
    ResolveSnapshotMeshes(snapshot);

    // Upload danych instancji SSBO — RAZ na klatkę, PRZED RenderShadowPass (który w fazie 2
    // czyta te same dane). Bufor zostaje zbindowany (BindBase) na binding 1 na całą klatkę,
    // batche adresują swój zakres uniformem "instanceBaseIndex" (patrz komentarz przy
    // mInstanceBuffer w Renderer.h). Zapas 2x, bo SetData/Resize realokują ilekroć liczba
    // elementów się zmienia — bez zapasu scena o zmiennej liczbie widocznych obiektów
    // realokowałaby bufor co klatkę.
    {
        PLU_PROFILE_SCOPE_GPU("Renderer::InstanceUpload");
        const Int32 neededInstances = static_cast<Int32>(snapshot->StaticInstanceData.Size());
        if (neededInstances > 0) {
            if (mInstanceBuffer.GetCount() < neededInstances) {
                mInstanceBuffer.Resize(neededInstances * 2);
            }
            mInstanceBuffer.Update(snapshot->StaticInstanceData);
        }
        mInstanceBuffer.BindBase(1);
    }

    const Matrix4 view = glm::inverse(
        glm::translate(glm::mat4(1.0f), snapshot->CameraLocation) *
        glm::mat4_cast(glm::quat(glm::radians(snapshot->CameraRotation)))
    );

    // Depth shaders resolved once for both shadow passes (see ResolveDepthShaders). A frame
    // without them simply produces no shadow frusta, so everything downstream reads "no shadows".
    ResolveDepthShaders();

    // --- Uniformy globalne, raz na klatkę, PRZED jakimkolwiek passem ---
    // Kamera, światło i czas trafiają na listę aktywnych shaderów prowadzoną przez ShadersManager
    // (Renderer nie trzyma własnej listy). Uniformy nieobecne w danym shaderze są no-opem
    // (location == -1), więc ustawianie ich na wszystkich programach jest bezpieczne.
    //
    // Blok stoi przed passami głębi, nie po nich: te rysują teraz materiałowymi vertex shaderami
    // (ResolveDepthProgram), a taki shader może czytać dowolny uniform globalny — `time` napędza
    // wiatr w trawie. Gdyby broadcast leciał po nich, cień i głębia falowałyby o klatkę za tym,
    // co widać na ekranie. View i projection ustawione tu są kamery; passy cieni podstawiają
    // sobie macierze swojego frustum na programach, których używają.
    DynamicArray<TUsePointer<ShaderProgram>>* activePrograms = mApplicationInfo->AppShaderManager->GetRenderableShaderPrograms();
    const UInt32 programCount = activePrograms ? activePrograms->Size() : 0;
    for (UInt32 p = 0; p < programCount; p++) {
        ShaderProgram* program = activePrograms->At(p).GetRaw();
        if (!program) continue;
        // Hot reload: main-thread zgłosił zmianę źródła, tu (na render threadzie z kontekstem GL)
        // rekompilujemy. Recompile przy błędzie kompilacji zostawia stary program załadowany.
        if (program->ConsumeRecompileRequest()) {
            program->Recompile();
        }
        if (!program->IsLoaded()) continue;

        program->SetMatrix4Uniform("view", view);
        program->SetMatrix4Uniform("projection", snapshot->CameraProjectionMatrix);
        program->SetVec3Uniform("cameraPos", snapshot->CameraLocation);
        program->SetFloatUniform("time", shaderTime);

        if (snapshot->HasDirLight) {
            program->SetVec3Uniform("dirLightDir", snapshot->DirLight.Direction);
            program->SetVec4Uniform("dirLightColor", Vec4(snapshot->DirLight.Color, snapshot->DirLight.Intensity));
        }

        // Material textures start at unit 0; the shadow array lives at kShadowTextureUnit, far
        // out of their way (see the comment there). Constant either way, so a frame without a
        // directional light does not silently renumber every material's samplers.
        program->SetSlotsUsed(0);
    }

    // Particles tick before any pass draws them: the shadow passes below and the main pass must
    // all see this frame's positions, or the shadows would trail the particles by a frame.
    TickParticleSpawners(snapshot, deltaTime, view);

    // Both shadow passes are planned BEFORE either draws: the caster culling below covers every
    // frustum of the frame in one sweep, so the cascade matrices and the spot slot matrices both
    // have to exist first. That is what keeps the visible-index SSBO a single upload on binding 3.
    PrepareShadowCascades(snapshot, view);
    PrepareSpotShadowSlots(snapshot);
    snapshot->StatCulledCount += CullShadowCasters(snapshot);

    // Pass 1: mapy głębi kaskad dla światła kierunkowego.
    RenderShadowPass(snapshot);
    // Pass 1b: depth slots of the spot shadow atlas. Shares the visible-index SSBO with the
    // cascades, so it must run after them and before anything rebinds binding 3.
    RenderSpotShadowPass(snapshot);

    // Shadow parameter block — pushed every frame, shadows or not (see UpdateShadowDataBuffer).
    UpdateShadowDataBuffer(snapshot);
    // Spot light blocks — same unconditional rule (see UpdateSpotLightBuffers).
    UpdateSpotLightBuffers(snapshot);

#ifdef PLU_ENGINE_EDITOR_BUILD
    // Debug cascade / spot slot viewers — no-op unless a panel asked for one this frame.
    UpdateShadowLayerView();
    UpdateSpotShadowSlotView();
#endif

    // Pass 2: scena do głównego bufora.
    PLU_PROFILE_SCOPE("Renderer::MainPass");
    PLU_PROFILE_SCOPE_GPU("Renderer::MainPass");
    mMainBuffer->Clear();

    // Pass 0: scene depth for contact shadows. Renders into its OWN framebuffer, so it neither
    // touches nor is touched by the main buffer's clear above.
    EnsureDepthPrepassBuffer();
    RenderDepthPrepass(snapshot, view, snapshot->CameraProjectionMatrix);

    mMainBuffer->Bind();

    // Tablica map cieni bindowana RAZ na klatkę na stały slot 0 wraz z samplerem porównującym
    // (to on robi z texture() sprzętowe PCF). Jednostki teksturujące to stan globalny GL, nie
    // per program, a slot samplera jest wpisany w shader przez layout(binding = 0) — w pętli
    // programów nie zostaje już nic per kaskada.
    if (mShadowAtlas) {
        mShadowAtlas->Bind(kShadowTextureUnit);
        mShadowCompareSampler.Bind(kShadowTextureUnit);
        CheckShadowGLError("Renderer::RenderSnapshot (shadow atlas bind)");
    }
    // Spot shadow atlas on its own unit. The SAME comparison sampler object serves both units —
    // glBindSampler binds one sampler object to a unit, and one object may sit on many units.
    if (mSpotShadowArray) {
        mSpotShadowArray->Bind(kSpotShadowTextureUnit);
        mShadowCompareSampler.Bind(kSpotShadowTextureUnit);
        CheckShadowGLError("Renderer::RenderSnapshot (spot shadow atlas bind)");
    }
    // Scene depth for contact shadows. NO comparison sampler here — unlike the shadow maps this is
    // read as a plain depth value to be ray-marched against, not compared against a reference.
    if (mSceneDepthTexture) {
        mSceneDepthTexture->Bind(kSceneDepthTextureUnit);
        CheckShadowGLError("Renderer::RenderSnapshot (scene depth bind)");
    }

    // Batche instancingu (grupowanie zrobione na main w RenderSnapshotBuilder::BatchStaticMeshes).
    // Materiały wchodzą w instancing OPT-IN: dopóki ich program nie ma bloku SSBO "InstanceMatrices"
    // (HasInstanceDataBlock), batch leci fallbackiem per-obiekt niżej — bajtowo zgodnym z dawną
    // pętlą po StaticMeshRenderObjects (ten shader MA `uniform mat4 model`).
    // Programy Z blokiem InstanceMatrices (BasicVertInstanced.vert) celowo NIE mają `uniform mat4
    // model` — transform idzie wyłącznie z SSBO. Dla takich programów instanced draw jest jedyną
    // poprawną ścieżką NIEZALEŻNIE od VisibleCount; SetMatrix4Uniform("model", ...) na nich byłby
    // cichym no-opem (lokacja -1), więc pojedyncza instancja renderowałaby się ze śmieciowym/starym
    // transformem z instances[instanceBaseIndex] zamiast własnego.
    const UInt32 staticBatchCount = snapshot->StaticMeshBatches.Size();
    for (UInt32 i = 0; i < staticBatchCount; i++) {
        StaticMeshBatch* batch = &snapshot->StaticMeshBatches[i];
        const TUsePointer<StaticMesh>& staticMesh = mResolvedBatchMeshes[i];
        TUsePointer<MaterialInfo> materialInfo = mApplicationInfo->AppAssetManager->GetAssetDataNoLoad(batch->MaterialUUID);
        if (!materialInfo || !staticMesh) continue;
        if (!staticMesh->IsLoaded) {
            mApplicationInfo->AppRenderingManager->RequestStaticMeshLoad(batch->MeshUUID);
        }
        TUsePointer<ShaderProgram> shaderProgram = mApplicationInfo->AppShaderManager->GetShaderProgram(materialInfo->shaderProgram);
        if (!shaderProgram || !shaderProgram->IsLoaded()) {
            // Leniwa kompilacja na render threadzie (analogicznie do RequestStaticMeshLoad dla meshy);
            // LoadShader rejestruje program w liście aktywnych ShadersManagera, więc w następnej
            // klatce dostanie uniformy globalne powyżej. Tego batcha ta klatka pomija.
            mApplicationInfo->AppShaderManager->LoadShader(materialInfo->shaderProgram);
            continue;
        }

        // Per-batch: tylko materiał (tekstury od slotu kCascadeCount), potem albo jeden
        // glDrawElementsInstanced, albo pętla po instancjach (fallback).
        shaderProgram->RenderFromMaterial(materialInfo.GetRaw(), mApplicationInfo->AppRenderingManager);

        const bool useInstancing = shaderProgram->HasInstanceDataBlock();
        if (useInstancing) {
            // Shader na aktualnej konwencji adresuje instancje przez VisibleInstanceIndices, więc
            // dostaje zakres kamery z CullShadowCasters — ten sam mechanizm, którym rysują passy
            // głębi, dzięki czemu ten sam vertex shader obsługuje wszystkie passy. Starszy shader
            // (bez tego bloku) indeksuje bufor instancji wprost i dostaje zakres batcha jak dotąd.
            const bool useVisibleIndices = shaderProgram->HasVisibleIndexBlock();
            UInt32 drawBaseIndex = batch->InstanceOffset;
            UInt32 drawCount     = batch->VisibleCount;
            if (useVisibleIndices && !mShadowDrawRanges.IsEmpty()) {
                const ShadowDrawRange& cameraRange = mShadowDrawRanges[CameraFrustumIndex() * staticBatchCount + i];
                drawBaseIndex = cameraRange.Offset;
                drawCount     = cameraRange.Count;
            } else if (!useVisibleIndices && batch->VisibleCount > 0
                       && !mWarnedLegacyInstancedPrograms.Contains(materialInfo->shaderProgram.getUUID())) {
                mWarnedLegacyInstancedPrograms.Insert(materialInfo->shaderProgram.getUUID());
                PLU_CORE_WARN("Instanced material {} uses shader program {} without the 'VisibleInstanceIndices' SSBO block. "
                              "It still renders, but the depth prepass and the shadow maps fall back to the engine depth shader, "
                              "so anything its vertex shader does to gl_Position (wind, vertex animation) is missing from the "
                              "depth buffer and from its shadow. Add the block and index instances through it "
                              "(see BasicVertInstanced.vert).",
                              batch->MaterialUUID.getUUID(), materialInfo->shaderProgram.getUUID());
            }
            if (drawCount > 0) {
                shaderProgram->SetIntUniform("instanceBaseIndex", static_cast<int>(drawBaseIndex));
                DrawStaticMeshInstanced(staticMesh.GetRaw(), mApplicationInfo->AppRenderingManager.GetRaw(), drawCount);
                snapshot->StatDrawCalls++;
                snapshot->StatInstancesDrawn += drawCount;
            }
        } else {
            for (UInt32 v = 0; v < batch->VisibleCount; v++) {
                const InstanceGPUData& instance = snapshot->StaticInstanceData[batch->InstanceOffset + v];
                shaderProgram->SetMatrix4Uniform("model", instance.ModelMatrix);
                // Macierz normalnych z CPU (snapshot ma ją już policzoną dla batchingu) —
                // BasicVert.vert nie robi już transpose(inverse()) per wierzchołek.
                shaderProgram->SetMatrix4Uniform("normalMatrix", instance.NormalMatrix);
                DrawStaticMesh(staticMesh.GetRaw(), mApplicationInfo->AppRenderingManager.GetRaw());
            }
            snapshot->StatDrawCalls += batch->VisibleCount;
            snapshot->StatInstancesDrawn += batch->VisibleCount;
            if (batch->VisibleCount > 1 && !mWarnedNonInstancedPrograms.Contains(materialInfo->shaderProgram.getUUID())) {
                mWarnedNonInstancedPrograms.Insert(materialInfo->shaderProgram.getUUID());
                PLU_CORE_WARN("Batch of {} static mesh instances uses material {} whose shader program {} has no 'InstanceMatrices' SSBO block — "
                              "falling back to one draw call per instance. Use a program with an instanced vertex shader (e.g. BasicVertInstanced.vert) to enable instancing.",
                              batch->VisibleCount, batch->MaterialUUID.getUUID(), materialInfo->shaderProgram.getUUID());
            }
        }
    }

    UInt64 skeletalMeshCount = snapshot->SkeletalMeshRenderObjects.Size();
    for (UInt32 i = 0; i < skeletalMeshCount; i++) {
        SkeletalMeshRenderObject* renderObject = &snapshot->SkeletalMeshRenderObjects[i];
        const TUsePointer<SkeletalMesh>& skeletalMesh = mResolvedSkeletalMeshes[i];
        TUsePointer<MaterialInfo> materialInfo = mApplicationInfo->AppAssetManager->GetAssetDataNoLoad(renderObject->MaterialUUID);
        if (!materialInfo || !skeletalMesh) continue;
        if (!skeletalMesh->IsLoaded) {
            mApplicationInfo->AppRenderingManager->RequestSkeletalMeshLoad(renderObject->MeshUUID);
        }
        TUsePointer<ShaderProgram> shaderProgram = mApplicationInfo->AppShaderManager->GetShaderProgram(materialInfo->shaderProgram);
        if (!shaderProgram || !shaderProgram->IsLoaded()) {
            // Leniwa kompilacja na render threadzie (analogicznie do RequestStaticMeshLoad dla meshy);
            // LoadShader rejestruje program w liście aktywnych ShadersManagera, więc w następnej
            // klatce dostanie uniformy globalne powyżej. Tej klatki mesh jest pomijany.
            mApplicationInfo->AppShaderManager->LoadShader(materialInfo->shaderProgram);
            continue;
        }

        // Materiał na programie bez skinningu (brak bloku SSBO "BoneMatrices" w vertex shaderze)
        // rysuje skeletal mesh zamrożony w bind pose — po cichu. Ostrzegamy raz per program.
        if (!shaderProgram->HasBoneMatricesBlock() && !mWarnedNonSkeletalPrograms.Contains(materialInfo->shaderProgram.getUUID())) {
            mWarnedNonSkeletalPrograms.Insert(materialInfo->shaderProgram.getUUID());
            PLU_CORE_WARN("Skeletal mesh {} uses material {} whose shader program {} has no 'BoneMatrices' SSBO block — "
                          "no skinning, mesh will stay in bind pose. Use a program with a skeletal vertex shader (e.g. BasicVertSkeletal.vert).",
                          renderObject->MeshUUID.getUUID(), renderObject->MaterialUUID.getUUID(), materialInfo->shaderProgram.getUUID());
        }

        // Per-mesh: tylko materiał (tekstury od slotu 1) + offset palety w buforze wysłanym raz
        // na klatkę (UploadSkeletalPalettes) + model + rysowanie.
        shaderProgram->RenderFromMaterial(materialInfo.GetRaw(), mApplicationInfo->AppRenderingManager);

        shaderProgram->SetIntUniform("paletteBaseIndex", static_cast<int>(mSkeletalPaletteRanges[i].Offset));
        shaderProgram->SetMatrix4Uniform("model", renderObject->ModelMatrix);
        // Normal matrix from the CPU — once per object instead of transpose(inverse()) per
        // vertex. Covers only the model transform; BasicVertSkeletal.vert applies skinning on top.
        shaderProgram->SetMatrix4Uniform("normalMatrix", glm::transpose(glm::inverse(renderObject->ModelMatrix)));
        DrawSkeletalMesh(skeletalMesh.GetRaw(), mApplicationInfo->AppRenderingManager.GetRaw());
    }

    RenderParticles(snapshot, view, snapshot->CameraProjectionMatrix * view);

    // Gathering walks every particle (~10 ms per million in Debug), and a panel read by a person
    // needs no 60 Hz — so at most every kParticleDebugStatsInterval. The timer keeps running while
    // nobody asks, so opening the panel publishes on the first request.
    mParticleDebugStatsTimer += deltaTime;
    if (ConsumeParticleDebugStatsRequest() && mParticleDebugStatsTimer >= kParticleDebugStatsInterval) {
        mParticleDebugStatsTimer = 0.0f;
        PublishParticleDebugStats(GatherParticleDebugStats(snapshot, deltaTime));
    }

#ifdef PLU_ENGINE_EDITOR_BUILD
    // Pass 3: editor grid, blended over the scene. Before debug geometry, so physics
    // wireframes/points draw on top of the grid.
    RenderEditorGrid(snapshot, view);
#endif

    // Transparent particle sprites and ribbons: after everything opaque (they test against its depth without
    // writing their own) and after the grid, so the grid does not paint over them.
    RenderTransparentParticles(snapshot, view, snapshot->CameraProjectionMatrix);

#ifdef PLU_ENGINE_EDITOR_BUILD
    // Pass 4: debugowa geometria fizyki (linie + punkty) do tego samego bufora.
    RenderDebugGeometry(snapshot, snapshot->CameraProjectionMatrix * view);
#endif

    mMainBuffer->Unbind();

    // Hand texture unit 0 back as plain, unbound state: the ImGui backend binds its own textures
    // there right after this (a comparison sampler left over it would render the whole editor UI
    // black), and the next frame's depth pass renders INTO this array.
    UnbindShadowTexture();
    UnbindSpotShadowTexture();
    UnbindSceneDepthTexture();
    CheckShadowGLError("Renderer::RenderSnapshot (frame end)");

    {
        PLU_PROFILE_SCOPE("Renderer::PostProcessPass");
        PLU_PROFILE_SCOPE_GPU("Renderer::PostProcessPass");
        //TODO
    }

    // Publikacja liczników tej klatki dla panelu Render/GPU (main thread) — snapshot->Stat*
    // było tylko roboczym akumulatorem powyżej, ta klatka jest teraz skończona.
    SetRenderFrameStats(snapshot->StatDrawCalls, snapshot->StatInstancesDrawn, snapshot->StatCulledCount);
    SetShadowCascadeStats(mCascadeCasterCounts.Data(), mCascadeCasterCounts.Size());
    SetSpotLightStats(mSpotShadowCasterCounts.Data(), mSpotShadowCasterCounts.Size(), snapshot->SpotLights.Size());
}

void Plu::Renderer::RenderDebugGeometry(Plu::RenderSnapshot *snapshot, const Matrix4 &viewProj)
{
    if (snapshot->DebugLineVerts.IsEmpty() && snapshot->DebugPointVerts.IsEmpty()) return;

    PLU_PROFILE_SCOPE("Renderer::RenderDebugGeometry");
    PLU_PROFILE_SCOPE_GPU("Renderer::RenderDebugGeometry");

    TUsePointer<ShaderProgram> shader = mApplicationInfo->AppShaderManager->GetShaderProgram(EngineAssets::DebugLine);
    if (!shader) return;
    if (!shader->IsLoaded()) {
        // Leniwa kompilacja na render threadzie (parytet z passem materiałów/cieni); rysowanie
        // pojawi się w kolejnej klatce, gdy shader będzie gotowy.
        mApplicationInfo->AppShaderManager->LoadShader(shader->Uuid);
        return;
    }

    shader->SetMatrix4Uniform("uViewProj", viewProj);

    glBindVertexArray(mDebugVao);
    glBindBuffer(GL_ARRAY_BUFFER, mDebugVbo);

    if (!snapshot->DebugLineVerts.IsEmpty()) {
        glBufferData(GL_ARRAY_BUFFER, snapshot->DebugLineVerts.Size() * sizeof(float),
                     snapshot->DebugLineVerts.Data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(snapshot->DebugLineVerts.Size() / 6));
    }

    if (!snapshot->DebugPointVerts.IsEmpty()) {
        glPointSize(snapshot->DebugPointSize);
        glBufferData(GL_ARRAY_BUFFER, snapshot->DebugPointVerts.Size() * sizeof(float),
                     snapshot->DebugPointVerts.Data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(snapshot->DebugPointVerts.Size() / 6));
        glPointSize(1.0f);
    }

    glBindVertexArray(0);
}

void Plu::Renderer::RenderEditorGrid(Plu::RenderSnapshot *snapshot, const Matrix4 &view)
{
    if (!snapshot->ShowEditorGrid) return;

    PLU_PROFILE_SCOPE("Renderer::RenderEditorGrid");
    PLU_PROFILE_SCOPE_GPU("Renderer::RenderEditorGrid");

    TUsePointer<ShaderProgram> shader = mApplicationInfo->AppShaderManager->GetShaderProgram(EngineAssets::EditorGridProgram);
    if (!shader) return;
    if (!shader->IsLoaded()) {
        // Leniwa kompilacja na render threadzie (parytet z DebugLine); siatka pojawi się
        // w kolejnej klatce, gdy shader będzie gotowy.
        mApplicationInfo->AppShaderManager->LoadShader(shader->Uuid);
        return;
    }

    const Matrix4 viewProj = snapshot->CameraProjectionMatrix * view;
    shader->SetMatrix4Uniform("uViewProj", viewProj);
    shader->SetMatrix4Uniform("uInvViewProj", glm::inverse(viewProj));
    shader->SetVec3Uniform("uCameraPos", snapshot->CameraLocation);

    // Depth test stays on — EditorGrid.frag writes the plane point's real depth, so scene
    // geometry occludes the grid. Depth writes go off for the pass: the blended grid must
    // not occlude anything drawn after it (debug geometry).
    glDepthMask(GL_FALSE);
    glBindVertexArray(mGridVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
}

void Plu::Renderer::Shutdown()
{
    DestroyParticleSpawners();
    DestroyDepthVariants();
    DestroyShadowResources();
    DestroySpotShadowResources();
    // Observer first — the depth texture is owned by the framebuffer destroyed right after.
    mSceneDepthTexture = nullptr;
    if (mDepthPrepassBuffer) {
        mApplicationInfo->AppObjectManager->DestroyObject(mDepthPrepassBuffer->GetObjectHandle());
        mDepthPrepassBuffer->Destroy();
        mDepthPrepassBuffer = nullptr;
    }
    mShadowCompareSampler.Destroy();
    if (mShadowLayerView) {
        mApplicationInfo->AppObjectManager->DestroyObject(mShadowLayerView->GetObjectHandle());
        mShadowLayerView->Destroy();
        mShadowLayerView = nullptr;
    }
    if (mSpotShadowView) {
        mApplicationInfo->AppObjectManager->DestroyObject(mSpotShadowView->GetObjectHandle());
        mSpotShadowView->Destroy();
        mSpotShadowView = nullptr;
    }
    mVisibleInstanceBuffer.Destroy();
    mCascades.Clear();
    mCascadeSplits.Clear();
    mShadowDataBuffer.Destroy();

    mSpotShadowSlotOwners.Clear();
    mSpotShadowMatrices.Clear();
    mSpotShadowViews.Clear();
    mSpotShadowProjs.Clear();
    mSpotShadowCasterCounts.Clear();
    mSpotLightBuffer.Destroy();
    mSpotLightIndexBuffer.Destroy();
    mSpotLightDataBuffer.Destroy();

    if (mDebugVao) { glDeleteVertexArrays(1, &mDebugVao); mDebugVao = 0; }
    if (mDebugVbo) { glDeleteBuffers(1, &mDebugVbo); mDebugVbo = 0; }
    if (mGridVao) { glDeleteVertexArrays(1, &mGridVao); mGridVao = 0; }
    if (mParticleSpriteVao) { glDeleteVertexArrays(1, &mParticleSpriteVao); mParticleSpriteVao = 0; }

    mApplicationInfo->AppObjectManager->DestroyObject(mMainBuffer->GetObjectHandle());
    mMainBuffer->Destroy();
    mMainBuffer = nullptr;
}
