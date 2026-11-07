//
// Created by Plutex on 8/31/26.
//

#ifndef PLUENGINE_PARTICLE_H
#define PLUENGINE_PARTICLE_H
#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "Particle.generated.h"

namespace Plu
{
    struct StaticMesh;

    // Which shadow passes a particle class takes part in. Cast draws the particles into the
    // directional cascades and the spot shadow slots; Receive darkens particles that sit in the
    // directional light's shadow (spot lights do not light particles, so their shadow is ignored).
    PLU_ENUM(PyExport, PyNamespace=Plu)
    enum class ParticleShadowMode : UInt8
    {
        None,
        Cast,
        Receive,
        CastAndReceive
    };

    PLU_STRUCT(PyExport)
    struct PLUEFFECTS_API ParticleClass
    {
        REFLECTION_BODY_PARTICLECLASS()

        // Seconds a particle lives.
        PLU_PROPERTY(PyExport)
        float Lifetime = 10;

        // Spawn with a velocity of LaunchStrength (m/s) in a uniformly random direction inside the
        // launch cone.
        PLU_PROPERTY(PyExport)
        bool LaunchOnSpawn = true;
        PLU_PROPERTY(PyExport)
        float LaunchStrength = 1.0f;
        // Half-angle of the launch cone in degrees, around the spawner's forward vector.
        // 0 = straight forward, 90 = hemisphere, 180 = every direction.
        PLU_PROPERTY(PyExport)
        float LaunchConeAngle = 180.0f;

        // Quadratic air drag: deceleration = Drag / Mass * speed^2 (1/m after dividing by mass).
        // Terminal speed under gravity g is sqrt(g * Mass / Drag).
        PLU_PROPERTY(PyExport)
        float Drag = 0.2f;
        // Per-particle drag spread, 0..1: each particle gets Drag * (1 +- DragRandomness).
        PLU_PROPERTY(PyExport)
        float DragRandomness = 0.4f;

        // Re-spawn the last burst every LoopLength seconds.
        PLU_PROPERTY(PyExport)
        bool Loop = true;
        PLU_PROPERTY(PyExport)
        float LoopLength = 1.0f;

        // Kill a particle once its speed (m/s) drops to KillWhenSlowSpeed. Only armed after the
        // particle has been faster than that, so particles spawned at rest are not killed at once.
        PLU_PROPERTY(PyExport)
        bool KillWhenSlow = true;
        PLU_PROPERTY(PyExport)
        float KillWhenSlowSpeed = 0.1f;

        // Acceleration in m/s^2, independent of mass.
        PLU_PROPERTY(PyExport)
        Vec3 Gravity = {0.0f,-9.8f,0.0f};
        // Kilograms. Only scales drag: a heavier particle keeps its speed longer.
        PLU_PROPERTY(PyExport)
        float Mass = 10.0f;

        // Appearance. Particles are drawn as opaque square points of PointSize pixels on screen,
        // whatever their distance. Every particle costs PointSize^2 fragments — with a lot of
        // particles this is the GPU cost to tune.
        PLU_PROPERTY(PyExport)
        Vec3 Color = {1.0f, 1.0f, 0.0f};
        PLU_PROPERTY(PyExport)
        float PointSize = 10.0f;

        // Shadows. PointSize is in screen pixels and means nothing in a shadow map, so the shadow
        // of one particle is a square of ShadowSize metres, whatever the camera distance.
        PLU_PROPERTY(PyExport)
        ParticleShadowMode ShadowMode = ParticleShadowMode::CastAndReceive;
        PLU_PROPERTY(PyExport)
        float ShadowSize = 0.1f;

        [[nodiscard]] bool CastsShadow() const
        {
            return ShadowMode == ParticleShadowMode::Cast || ShadowMode == ParticleShadowMode::CastAndReceive;
        }
        [[nodiscard]] bool ReceivesShadow() const
        {
            return ShadowMode == ParticleShadowMode::Receive || ShadowMode == ParticleShadowMode::CastAndReceive;
        }
    };
}

#endif //PLUENGINE_PARTICLE_H
