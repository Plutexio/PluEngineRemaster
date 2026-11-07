//
// Created by Plutex on 2026-09-15.
//

#ifndef PLUENGINE_PARTICLEPOINTBUFFER_H
#define PLUENGINE_PARTICLEPOINTBUFFER_H

#include "PluEngine/Core.h"

namespace Plu
{
    // GL buffer of one particle spawner: a VAO + VBO of positions only (attribute 0, vec3), drawn as
    // GL_POINTS. Render thread only — every method does GL.
    //
    // A plain handle, not a move-only RAII wrapper like ShaderStorageBuffer: it is stored as a value
    // in a HashMap, which copies values when it rehashes. Copies share the same GL objects, so
    // call Destroy exactly once, when the spawner goes away.
    struct PLURENDER_API ParticlePointBuffer
    {
        UInt32 VertexArray = 0;
        UInt32 VertexBuffer = 0;
        // Particles the GPU storage has room for. Grows geometrically, never shrinks.
        UInt32 Capacity = 0;
        // Particles uploaded by the last Upload, i.e. what Draw draws.
        UInt32 Count = 0;

        // Uploads count particles of interleaved xyz (count * 3 floats). Creates the GL objects on
        // first use. The storage is orphaned every call (same size, so the driver recycles it)
        // instead of being overwritten in place, which would wait for the GPU to finish reading the
        // previous frame's contents.
        void Upload(const float* positions, UInt32 count);
        // Binds its own VAO, draws Count points and unbinds. The caller sets program and uniforms.
        void Draw() const;
        void Destroy();
    };
}

#endif //PLUENGINE_PARTICLEPOINTBUFFER_H
