#pragma once
#include <cstdint>
#include <random>

// Lightweight, header-only random number generation for PluSTL.
// The engine is thread_local — there is no synchronization, so drawing from any
// thread is safe, but sequences are not shared between threads.
namespace PluRandom
{
    inline std::mt19937_64& Engine()
    {
        thread_local std::mt19937_64 engine = [] {
            std::random_device device;
            return std::mt19937_64((static_cast<uint64_t>(device()) << 32) ^ device());
        }();
        return engine;
    }

    // Seeds this thread's engine — deterministic draws (tests, replay).
    inline void Seed(uint64_t seed) { Engine().seed(seed); }

    inline uint64_t NextUInt64() { return Engine()(); }

    // Random index in [0, size). Returns 0 for size == 0 — check emptiness yourself.
    inline size_t NextIndex(size_t size)
    {
        if (size == 0) return 0;
        return static_cast<size_t>(Engine()() % size);
    }

    // Random int in [min, max] (both ends inclusive). Reversed bounds are normalized.
    inline int64_t NextInt(int64_t min, int64_t max)
    {
        if (min > max) { const int64_t t = min; min = max; max = t; }
        return std::uniform_int_distribution<int64_t>(min, max)(Engine());
    }

    // Random float in [min, max). Reversed bounds are normalized.
    inline float NextFloat(float min = 0.0f, float max = 1.0f)
    {
        if (min > max) { const float t = min; min = max; max = t; }
        return std::uniform_real_distribution<float>(min, max)(Engine());
    }

    // Coin flip with the given success probability.
    inline bool NextBool(float probability = 0.5f)
    {
        return NextFloat(0.0f, 1.0f) < probability;
    }

    // xoshiro128+ — a small, fast, non-cryptographic generator for hot loops (particle spawning).
    // A value type with its own 128-bit state: own one per object or per thread, it is not
    // thread-safe and never touches the thread_local engine above. Draws are plain inline
    // arithmetic, with no distribution object and no TLS lookup per call.
    // Only the upper bits of xoshiro128+ are high quality, so every draw below uses those.
    class FastRandom
    {
    public:
        // Seeded from the thread's engine, so two instances never share a sequence.
        FastRandom() { Seed(NextUInt64()); }
        explicit FastRandom(uint64_t seed) { Seed(seed); }

        // splitmix64 expands the seed, so any value (0 included) gives a valid, non-zero state.
        void Seed(uint64_t seed)
        {
            for (uint32_t& word : mState) {
                seed += 0x9E3779B97F4A7C15ull;
                uint64_t z = seed;
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
                word = static_cast<uint32_t>((z ^ (z >> 31)) >> 32);
            }
        }

        uint32_t NextUInt32()
        {
            const uint32_t result = mState[0] + mState[3];
            const uint32_t t = mState[1] << 9;
            mState[2] ^= mState[0];
            mState[3] ^= mState[1];
            mState[1] ^= mState[2];
            mState[0] ^= mState[3];
            mState[2] ^= t;
            mState[3] = (mState[3] << 11) | (mState[3] >> 21);
            return result;
        }

        // Float in [0, 1): the top 24 bits, exactly representable in a float.
        float NextFloat()
        {
            return static_cast<float>(NextUInt32() >> 8) * 0x1.0p-24f;
        }

        // Float in [min, max). Bounds are not normalized — pass min <= max.
        float NextFloat(float min, float max)
        {
            return min + (max - min) * NextFloat();
        }

    private:
        uint32_t mState[4];
    };
}
