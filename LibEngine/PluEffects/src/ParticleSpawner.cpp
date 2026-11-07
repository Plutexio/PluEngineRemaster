//
// Created by Plutex on 8/31/26.
//

#include "PluEngine/Effects/Particles/ParticleSpawner.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "PluEngine/PluUtils.h"
#include "PluEngine/Timer.h"

namespace
{
    // Longest step a single tick may integrate. A render hitch (window drag, breakpoint, shader
    // compile) would otherwise launch every particle across the scene in one step.
    constexpr float kMaxParticleDeltaTime = 0.1f;
    // Shortest allowed loop period, so a LoopLength of ~0 cannot spawn a burst per loop iteration forever.
    constexpr float kMinLoopLength = 0.01f;
    // Mass floor; drag divides by it.
    constexpr float kMinParticleMass = 0.001f;
    constexpr float kTwoPi = 6.2831853f;
}

void Plu::ParticleSpawner::SetParticleClass(const ParticleClass& particleClass)
{
    mParticleClass = particleClass;
}

void Plu::ParticleSpawner::SetTransform(const Vec3& location, const Vec3& launchDirection)
{
    mLocation = location;
    const float length = glm::length(launchDirection);
    mLaunchDirection = length > 0.0001f ? launchDirection / length : Vec3(0.0f, 0.0f, -1.0f);
}

void Plu::ParticleSpawner::SyncSpawnRequests(UInt64 requestedParticles, int lastBurstSize)
{
    mLastParticleSpawned = std::max(lastBurstSize, 0);
    // The counter never decreases for one component, so going backwards means it was recreated
    // under the same UUID (scene reload) — start counting from zero again.
    if (requestedParticles < mSyncedRequestedParticles) {
        mSyncedRequestedParticles = 0;
    }
    if (requestedParticles == mSyncedRequestedParticles) return;
    const UInt64 missing = requestedParticles - mSyncedRequestedParticles;
    mSyncedRequestedParticles = requestedParticles;
    SpawnParticles(static_cast<int>(std::min<UInt64>(missing, static_cast<UInt64>(std::numeric_limits<int>::max()))));
}

void Plu::ParticleSpawner::EnsureCapacity(UInt32 particleCount)
{
    const size_t currentCapacity = mLifetimes.Size();
    if (particleCount <= currentCapacity) return;
    PLU_PROFILE_SCOPE("ParticleSpawner::EnsureCapacity");

    // Geometric growth: a spawner that keeps adding bursts reallocates O(log n) times, not per burst.
    const size_t newCapacity = std::max<size_t>(particleCount, currentCapacity * 2);
    mPositions.Resize(newCapacity * 3);
    mVelocitiesX.Resize(newCapacity);
    mVelocitiesY.Resize(newCapacity);
    mVelocitiesZ.Resize(newCapacity);
    mDrags.Resize(newCapacity);
    mLifetimes.Resize(newCapacity);
    mSlowKillArmed.Resize(newCapacity);
}

void Plu::ParticleSpawner::SpawnParticles(int numParticles)
{
    if (numParticles <= 0) return;
    PLU_PROFILE_SCOPE("ParticleSpawner::SpawnParticles");

    const UInt32 spawnCount = static_cast<UInt32>(std::min<UInt64>(
        static_cast<UInt64>(numParticles), std::numeric_limits<UInt32>::max() - static_cast<UInt64>(mAliveCount)));
    const UInt32 first = mAliveCount;
    const UInt32 end = first + spawnCount;
    EnsureCapacity(end);

    const float lifetime = mParticleClass.Lifetime;
    const float drag = mParticleClass.Drag;
    const float dragRandomness = std::clamp(mParticleClass.DragRandomness, 0.0f, 1.0f);
    const float launchStrength = mParticleClass.LaunchOnSpawn ? mParticleClass.LaunchStrength : 0.0f;

    // Launch cone basis, once per burst. A uniformly distributed direction inside a cone of the given
    // half-angle (Archimedes: on a sphere, z uniform in [cos(halfAngle), 1] with a uniform azimuth
    // covers the cap evenly). Normalizing a random point of a cube instead biases toward its corners.
    const Vec3 axis = mLaunchDirection;
    const Vec3 reference = std::abs(axis.y) > 0.99f ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(0.0f, 1.0f, 0.0f);
    const Vec3 right = glm::normalize(glm::cross(axis, reference));
    const Vec3 up = glm::cross(right, axis);
    const float minCosine = std::cos(glm::radians(std::clamp(mParticleClass.LaunchConeAngle, 0.0f, 180.0f)));

    float* positions = mPositions.Data();
    float* velocitiesX = mVelocitiesX.Data();
    float* velocitiesY = mVelocitiesY.Data();
    float* velocitiesZ = mVelocitiesZ.Data();
    float* drags = mDrags.Data();
    float* lifetimes = mLifetimes.Data();
    UInt8* slowKillArmed = mSlowKillArmed.Data();

    for (UInt32 i = first; i < end; i++) {
        positions[i * 3] = mLocation.x;
        positions[i * 3 + 1] = mLocation.y;
        positions[i * 3 + 2] = mLocation.z;
        lifetimes[i] = lifetime;
        slowKillArmed[i] = 0;
        drags[i] = std::max(0.0f, drag * (1.0f + mRandom.NextFloat(-1.0f, 1.0f) * dragRandomness));

        if (launchStrength != 0.0f) {
            const float z = mRandom.NextFloat(minCosine, 1.0f);
            const float azimuth = mRandom.NextFloat() * kTwoPi;
            const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float a = radius * std::cos(azimuth) * launchStrength;
            const float b = radius * std::sin(azimuth) * launchStrength;
            const float c = z * launchStrength;
            velocitiesX[i] = right.x * a + up.x * b + axis.x * c;
            velocitiesY[i] = right.y * a + up.y * b + axis.y * c;
            velocitiesZ[i] = right.z * a + up.z * b + axis.z * c;
        } else {
            velocitiesX[i] = 0.0f;
            velocitiesY[i] = 0.0f;
            velocitiesZ[i] = 0.0f;
        }
    }
    mAliveCount = end;
}

Plu::ParticleSpawnerDebugStats Plu::ParticleSpawner::GatherDebugStats() const
{
    PLU_PROFILE_SCOPE("ParticleSpawner::GatherDebugStats");

    ParticleSpawnerDebugStats stats;
    stats.UUID = UUID.getUUID();
    stats.AliveParticles = mAliveCount;
    stats.PoolSize = static_cast<UInt32>(mLifetimes.Size());
    stats.FreeSlots = stats.PoolSize - mAliveCount;
    stats.SyncedRequestedParticles = mSyncedRequestedParticles;
    stats.LastBurstSize = mLastParticleSpawned;
    stats.LoopTime = mLoopTime;
    stats.LoopLength = std::max(mParticleClass.LoopLength, kMinLoopLength);
    stats.Loop = mParticleClass.Loop;
    stats.Location = mLocation;
    stats.LaunchDirection = mLaunchDirection;
    if (mAliveCount == 0) return stats;

    // Raw pointers and plain comparisons, like the tick: with glm::min/max and container calls per
    // particle this walk cost several ticks' worth of time in Debug, every frame the panel is open.
    const float* positions = mPositions.Data();
    const float* velocitiesX = mVelocitiesX.Data();
    const float* velocitiesY = mVelocitiesY.Data();
    const float* velocitiesZ = mVelocitiesZ.Data();
    const float* lifetimes = mLifetimes.Data();

    float minX = positions[0], minY = positions[1], minZ = positions[2];
    float maxX = minX, maxY = minY, maxZ = minZ;
    float minLifetime = lifetimes[0], maxLifetime = lifetimes[0];
    float maxSpeed = 0.0f;
    float speedSum = 0.0f;
    const UInt32 count = mAliveCount;
    for (UInt32 i = 0; i < count; i++) {
        const float x = positions[i * 3];
        const float y = positions[i * 3 + 1];
        const float z = positions[i * 3 + 2];
        minX = x < minX ? x : minX;
        minY = y < minY ? y : minY;
        minZ = z < minZ ? z : minZ;
        maxX = x > maxX ? x : maxX;
        maxY = y > maxY ? y : maxY;
        maxZ = z > maxZ ? z : maxZ;

        const float lifetime = lifetimes[i];
        minLifetime = lifetime < minLifetime ? lifetime : minLifetime;
        maxLifetime = lifetime > maxLifetime ? lifetime : maxLifetime;

        const float speed = std::sqrt(velocitiesX[i] * velocitiesX[i] + velocitiesY[i] * velocitiesY[i] + velocitiesZ[i] * velocitiesZ[i]);
        maxSpeed = speed > maxSpeed ? speed : maxSpeed;
        speedSum += speed;
    }
    stats.BoundsMin = Vec3(minX, minY, minZ);
    stats.BoundsMax = Vec3(maxX, maxY, maxZ);
    stats.MinLifetimeLeft = minLifetime;
    stats.MaxLifetimeLeft = maxLifetime;
    stats.MaxSpeed = maxSpeed;
    stats.AverageSpeed = speedSum / static_cast<float>(mAliveCount);
    return stats;
}

void Plu::ParticleSpawner::RemoveDeadParticles()
{
    PLU_PROFILE_SCOPE("ParticleSpawner::RemoveDeadParticles");

    float* positions = mPositions.Data();
    float* velocitiesX = mVelocitiesX.Data();
    float* velocitiesY = mVelocitiesY.Data();
    float* velocitiesZ = mVelocitiesZ.Data();
    float* drags = mDrags.Data();
    float* lifetimes = mLifetimes.Data();
    UInt8* slowKillArmed = mSlowKillArmed.Data();

    // Swap-remove: the last alive particle takes the dead one's slot and index i is checked again,
    // since the moved particle may be dead too. Costs one move per death, nothing per survivor.
    UInt32 count = mAliveCount;
    UInt32 i = 0;
    while (i < count) {
        if (!(lifetimes[i] <= 0.0f)) {
            i++;
            continue;
        }
        count--;
        positions[i * 3] = positions[count * 3];
        positions[i * 3 + 1] = positions[count * 3 + 1];
        positions[i * 3 + 2] = positions[count * 3 + 2];
        velocitiesX[i] = velocitiesX[count];
        velocitiesY[i] = velocitiesY[count];
        velocitiesZ[i] = velocitiesZ[count];
        drags[i] = drags[count];
        lifetimes[i] = lifetimes[count];
        slowKillArmed[i] = slowKillArmed[count];
    }
    mAliveCount = count;
}

void Plu::ParticleSpawner::TickParticles(float deltaTime)
{
    PLU_PROFILE_SCOPE("ParticleSpawner::TickParticles");
    deltaTime = std::clamp(deltaTime, 0.0f, kMaxParticleDeltaTime);

    // Loop: repeat the last burst every LoopLength seconds. The remainder carries over, so the
    // spawn rate does not drift with the frame rate.
    if (mParticleClass.Loop && mLastParticleSpawned > 0) {
        const float loopLength = std::max(mParticleClass.LoopLength, kMinLoopLength);
        mLoopTime += deltaTime;
        while (mLoopTime >= loopLength) {
            mLoopTime -= loopLength;
            SpawnParticles(mLastParticleSpawned);
        }
    }

    if (mAliveCount == 0) return;

    {
        PLU_PROFILE_SCOPE("ParticleSpawner::Integrate");

        // Everything the loop reads is a local or a raw pointer: no member access, no glm and no
        // container calls per particle, so it stays cheap in Debug and vectorizes in Release.
        const float gravityX = mParticleClass.Gravity.x * deltaTime;
        const float gravityY = mParticleClass.Gravity.y * deltaTime;
        const float gravityZ = mParticleClass.Gravity.z * deltaTime;
        const float dragFactor = deltaTime / std::max(mParticleClass.Mass, kMinParticleMass);
        const UInt8 killWhenSlow = mParticleClass.KillWhenSlow ? 1 : 0;
        // Compared squared, to skip a second sqrt. A negative threshold means "always fast enough".
        const float killSpeed = mParticleClass.KillWhenSlowSpeed;
        const float killSpeedSquared = killSpeed >= 0.0f ? killSpeed * killSpeed : -1.0f;

        float* positions = mPositions.Data();
        float* velocitiesX = mVelocitiesX.Data();
        float* velocitiesY = mVelocitiesY.Data();
        float* velocitiesZ = mVelocitiesZ.Data();
        const float* drags = mDrags.Data();
        float* lifetimes = mLifetimes.Data();
        UInt8* slowKillArmed = mSlowKillArmed.Data();
        const UInt32 count = mAliveCount;

        // Branch-free; deaths only zero the lifetime and RemoveDeadParticles compacts afterwards.
        for (UInt32 i = 0; i < count; i++) {
            // Semi-implicit Euler: velocity first, then position with the new velocity.
            // Gravity is a constant acceleration, so the explicit step is fine.
            float velocityX = velocitiesX[i] + gravityX;
            float velocityY = velocitiesY[i] + gravityY;
            float velocityZ = velocitiesZ[i] + gravityZ;

            // Quadratic drag, dv/dt = -k|v|v with k = Drag / Mass. Solved implicitly:
            // v' = v / (1 + k|v|dt) — exact for drag alone, never overshoots or reverses the
            // velocity, and stable for any step (explicit Euler blows up once k|v|dt > 2).
            const float speed = std::sqrt(velocityX * velocityX + velocityY * velocityY + velocityZ * velocityZ);
            const float dragScale = 1.0f / (1.0f + drags[i] * dragFactor * speed);
            velocityX *= dragScale;
            velocityY *= dragScale;
            velocityZ *= dragScale;

            velocitiesX[i] = velocityX;
            velocitiesY[i] = velocityY;
            velocitiesZ[i] = velocityZ;
            positions[i * 3] += velocityX * deltaTime;
            positions[i * 3 + 1] += velocityY * deltaTime;
            positions[i * 3 + 2] += velocityZ * deltaTime;

            // KillWhenSlow: killed once slow, but only if it was fast before this step.
            const float newSpeedSquared = velocityX * velocityX + velocityY * velocityY + velocityZ * velocityZ;
            const UInt8 fast = newSpeedSquared > killSpeedSquared ? 1 : 0;
            const UInt8 slowKill = killWhenSlow & slowKillArmed[i] & (fast ^ 1);
            slowKillArmed[i] |= fast & killWhenSlow;
            lifetimes[i] = slowKill ? 0.0f : lifetimes[i] - deltaTime;
        }
    }

    RemoveDeadParticles();
}
