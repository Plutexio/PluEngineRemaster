#version 450 core

// Particle ribbons: an attribute-less VAO drawn as a GL_TRIANGLE_STRIP, two vertices per ribbon point.
// gl_VertexID / 2 = point along the ribbon, gl_VertexID & 1 = side.
//
// PerEmitter (uRibbonMode 1): one strip through every live particle, in spawn order (ordered storage):
//   glDrawArrays(GL_TRIANGLE_STRIP, 0, 2 * particles). Point k = particle k.
// PerParticle (uRibbonMode 0): one strip per particle through its trail samples:
//   glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 2 * samples, particles). Point k = sample k of particle
//   gl_InstanceID, sample 0 at the particle.

// Must stay layout-identical to Plu::ParticleInstanceGPU (ParticleInstanceBuffer.h), 48 B. Ribbons read
// positionRotation.w as texture u (normalised age) and sizeAndVelocity.x as half width.
struct ParticleInstance
{
    vec4 positionRotation;
    vec4 sizeAndVelocity;
    uint colorRGBA8;
    uint subUVFrameFlags;
    uint pad0;
    uint pad1;
};

layout(std430, binding = 7) readonly buffer ParticleInstances
{
    ParticleInstance particles[];
};

// PerParticle trails: uPointCount vec4 samples per particle (Plu::ParticleRibbonHistoryBuffer).
layout(std430, binding = 8) readonly buffer RibbonHistory
{
    vec4 history[];
};

uniform mat4 uViewProj;
uniform vec3 uCameraPos;
// 0 = PerParticle, 1 = PerEmitter.
uniform int uRibbonMode;
// Points along one ribbon: particles (PerEmitter) or trail samples (PerParticle).
uniform int uPointCount;

out vec2 vUV;
out vec4 vColor;

vec3 PointAt(int k)
{
    k = clamp(k, 0, uPointCount - 1);
    if (uRibbonMode == 1) return particles[k].positionRotation.xyz;
    return history[gl_InstanceID * uPointCount + k].xyz;
}

void main()
{
    int k = gl_VertexID >> 1;
    float side = (gl_VertexID & 1) == 0 ? -1.0 : 1.0;

    vec3 position = PointAt(k);
    // Central difference; the ends use their one neighbour.
    vec3 tangent = PointAt(k + 1) - PointAt(k - 1);
    if (dot(tangent, tangent) < 1e-12) tangent = vec3(0.0, 1.0, 0.0);

    // Camera-facing: the width runs across the tangent, perpendicular to the view ray.
    vec3 across = cross(tangent, uCameraPos - position);
    if (dot(across, across) < 1e-12) across = cross(tangent, vec3(0.0, 1.0, 0.0));
    if (dot(across, across) < 1e-12) across = vec3(1.0, 0.0, 0.0);
    across = normalize(across);

    ParticleInstance owner = particles[uRibbonMode == 1 ? k : gl_InstanceID];
    float halfWidth = owner.sizeAndVelocity.x;
    vec4 color = unpackUnorm4x8(owner.colorRGBA8);
    float u;
    if (uRibbonMode == 1)
    {
        u = owner.positionRotation.w;
    }
    else
    {
        // A trail thins and fades towards its tail.
        u = float(k) / float(max(uPointCount - 1, 1));
        halfWidth *= 1.0 - u;
        color.a *= 1.0 - u;
    }

    gl_Position = uViewProj * vec4(position + across * (side * halfWidth), 1.0);
    vUV = vec2(u, side * 0.5 + 0.5);
    vColor = color;
}
