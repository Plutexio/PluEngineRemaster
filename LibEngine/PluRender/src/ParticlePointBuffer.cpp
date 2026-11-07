//
// Created by Plutex on 2026-09-15.
//

#include "PluEngine/Render/ParticlePointBuffer.h"

#include <algorithm>
#include <glad/glad.h>

#include "PluEngine/Timer.h"

void Plu::ParticlePointBuffer::Upload(const float* positions, UInt32 count)
{
    PLU_PROFILE_SCOPE("ParticlePointBuffer::Upload");
    Count = count;
    if (count == 0) return;

    if (VertexArray == 0) {
        glGenVertexArrays(1, &VertexArray);
        glGenBuffers(1, &VertexBuffer);
        glBindVertexArray(VertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, VertexBuffer);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindVertexArray(0);
    }

    glBindBuffer(GL_ARRAY_BUFFER, VertexBuffer);
    // Grow with headroom, so a spawner whose particle count wobbles does not reallocate every frame.
    if (count > Capacity) {
        Capacity = std::max(count, Capacity * 2);
    }
    const GLsizeiptr capacityBytes = static_cast<GLsizeiptr>(Capacity) * 3 * sizeof(float);
    glBufferData(GL_ARRAY_BUFFER, capacityBytes, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(count) * 3 * sizeof(float), positions);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Plu::ParticlePointBuffer::Draw() const
{
    if (Count == 0 || VertexArray == 0) return;
    glBindVertexArray(VertexArray);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(Count));
    glBindVertexArray(0);
}

void Plu::ParticlePointBuffer::Destroy()
{
    if (VertexArray) { glDeleteVertexArrays(1, &VertexArray); VertexArray = 0; }
    if (VertexBuffer) { glDeleteBuffers(1, &VertexBuffer); VertexBuffer = 0; }
    Capacity = 0;
    Count = 0;
}
