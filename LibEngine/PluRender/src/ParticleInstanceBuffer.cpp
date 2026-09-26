//
// Created by Plutex on 2026-09-26.
//

#include "PluEngine/Render/ParticleInstanceBuffer.h"

#include <algorithm>
#include <glad/glad.h>

#include "PluEngine/Timer.h"

namespace
{
    // Creates the buffer on first use, grows it x2 when needed and orphans the storage every call (same
    // size, so the driver recycles it) rather than waiting for the GPU to finish reading last frame's data.
    void UploadStreamSSBO(UInt32& buffer, UInt64& capacityBytes, const void* data, UInt64 bytes)
    {
        if (buffer == 0) glGenBuffers(1, &buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        if (bytes > capacityBytes) capacityBytes = std::max<UInt64>(bytes, capacityBytes * 2);
        glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(capacityBytes), nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(bytes), data);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
}

void Plu::ParticleInstanceBuffer::Upload(const ParticleInstanceGPU* instances, UInt32 count)
{
    PLU_PROFILE_SCOPE("ParticleInstanceBuffer::Upload");
    Count = count;
    if (count == 0) return;
    // Grow with headroom, so an emitter whose particle count wobbles does not reallocate every frame.
    UInt64 capacityBytes = static_cast<UInt64>(Capacity) * sizeof(ParticleInstanceGPU);
    UploadStreamSSBO(Buffer, capacityBytes, instances, static_cast<UInt64>(count) * sizeof(ParticleInstanceGPU));
    Capacity = static_cast<UInt32>(capacityBytes / sizeof(ParticleInstanceGPU));
}

void Plu::ParticleInstanceBuffer::Bind() const
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kParticleInstanceBinding, Buffer);
}

void Plu::ParticleInstanceBuffer::Destroy()
{
    if (Buffer) { glDeleteBuffers(1, &Buffer); Buffer = 0; }
    Capacity = 0;
    Count = 0;
}

void Plu::ParticleRibbonHistoryBuffer::Upload(const float* samples, UInt32 particles, UInt32 samplesPerParticle)
{
    PLU_PROFILE_SCOPE("ParticleRibbonHistoryBuffer::Upload");
    Particles = particles;
    SamplesPerParticle = samplesPerParticle;
    if (particles == 0 || samplesPerParticle == 0) return;
    UploadStreamSSBO(Buffer, CapacityBytes, samples, static_cast<UInt64>(particles) * samplesPerParticle * 4 * sizeof(float));
}

void Plu::ParticleRibbonHistoryBuffer::Bind() const
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kParticleRibbonHistoryBinding, Buffer);
}

void Plu::ParticleRibbonHistoryBuffer::Destroy()
{
    if (Buffer) { glDeleteBuffers(1, &Buffer); Buffer = 0; }
    CapacityBytes = 0;
    Particles = 0;
    SamplesPerParticle = 0;
}
