//
// Created by Plutex on 2026-09-26.
//

#ifndef PLUENGINE_PARTICLEINSTANCEBUFFER_H
#define PLUENGINE_PARTICLEINSTANCEBUFFER_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"

namespace Plu
{
    // SSBO binding the particle sprite / ribbon shaders read their instances from. 0-6 are taken
    // (bone palettes, static instances, ShadowData, visible indices, SpotLightData, SpotLights,
    // SpotLightIndices) — binding a block to a taken index fails silently, the shader just reads
    // someone else's bytes.
    constexpr UInt32 kParticleInstanceBinding = 7;
    // Per-particle ribbon trails (vec4 samples, see ParticleRibbonHistoryBuffer).
    constexpr UInt32 kParticleRibbonHistoryBinding = 8;

    // One particle as the sprite / ribbon shaders see it (std430 `ParticleInstance` in ParticleSprite.vert
    // and ParticleRibbon.vert). No vec3 anywhere: 12 B in C++ against a 16 B stride in std430 would
    // misalign every element after the first.
    //
    // Ribbons reuse it with their own meaning of two fields: PositionRotation.w = texture u along the
    // ribbon (normalised age), SizeAndVelocity.x = half width.
    struct ParticleInstanceGPU
    {
        Vec4   PositionRotation; // xyz world position, w rotation (radians) — ribbons: texture u
        Vec4   SizeAndVelocity;  // xy half extents, zw view-space motion (m/s) for velocity stretch — ribbons: x half width
        UInt32 ColorRGBA8;       // packUnorm4x8 order: r in the low byte
        UInt32 SubUVFrameFlags;  // low 16 bits sub-UV frame, high 16 bits flags (none yet)
        UInt32 Pad0;
        UInt32 Pad1;
    };
    static_assert(sizeof(ParticleInstanceGPU) == 48, "ParticleInstanceGPU must match the std430 ParticleInstance");

    // GPU instances of one emitter, drawn as instanced quads (attribute-less VAO, gl_InstanceID
    // indexes the SSBO). Render thread only — every method does GL.
    //
    // A plain handle, not a move-only RAII wrapper like ShaderStorageBuffer: it lives by value in
    // containers the renderer copies (the spawner map rehashes). Copies share the GL buffer, so
    // call Destroy exactly once, when the emitter goes away.
    struct PLURENDER_API ParticleInstanceBuffer
    {
        UInt32 Buffer = 0;
        // Instances the GPU storage has room for. Grows geometrically, never shrinks.
        UInt32 Capacity = 0;
        // Instances uploaded by the last Upload, i.e. what gets drawn.
        UInt32 Count = 0;

        // Uploads count instances. Creates the buffer on first use; orphans the storage every call
        // (same size, so the driver recycles it) rather than waiting for the GPU to finish reading
        // the previous frame's contents.
        void Upload(const ParticleInstanceGPU* instances, UInt32 count);
        // Binds the buffer to kParticleInstanceBinding.
        void Bind() const;
        void Destroy();

        [[nodiscard]] UInt64 GetAllocatedBytes() const { return static_cast<UInt64>(Capacity) * sizeof(ParticleInstanceGPU); }
    };

    // Trail samples of a per-particle ribbon emitter: particle i owns SamplesPerParticle vec4s starting at
    // i * SamplesPerParticle, sample 0 at the particle, the last one the tail. Same handle rules as
    // ParticleInstanceBuffer.
    struct PLURENDER_API ParticleRibbonHistoryBuffer
    {
        UInt32 Buffer = 0;
        UInt64 CapacityBytes = 0;
        UInt32 Particles = 0;
        UInt32 SamplesPerParticle = 0;

        // floats = particles * samplesPerParticle * 4.
        void Upload(const float* samples, UInt32 particles, UInt32 samplesPerParticle);
        // Binds the buffer to kParticleRibbonHistoryBinding.
        void Bind() const;
        void Destroy();
    };
}

#endif //PLUENGINE_PARTICLEINSTANCEBUFFER_H
