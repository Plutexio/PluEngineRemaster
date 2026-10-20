//
// Created by Plutex on 2026-08-06.
//

#ifndef PLUSTL_LOCKPRIMITIVES_H
#define PLUSTL_LOCKPRIMITIVES_H

#include <atomic>
#include <cstddef>
#include <new>
#include <thread>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #if defined(_MSC_VER)
        #include <intrin.h>
        #define PLU_CPU_PAUSE() _mm_pause()
    #else
        #define PLU_CPU_PAUSE() __builtin_ia32_pause()
    #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define PLU_CPU_PAUSE() __asm__ __volatile__("yield" ::: "memory")
#else
    #define PLU_CPU_PAUSE() ((void)0)
#endif

namespace Plu
{
    // Cache-line size used for padding shared atomics apart, so two hot counters never
    // land in the same line (false sharing). Same idiom as
    // PluEngine/Threading/TripleBuffer.h.
#if defined(__cpp_lib_hardware_interference_size)
    inline constexpr std::size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
    inline constexpr std::size_t kCacheLineSize = 64;
#endif

    // ========================================================================
    // SpinBackoff
    // ========================================================================
    // The one wait policy shared by everything in Concurrent/: burn a few `pause`
    // instructions, then hand the timeslice back. Past the spin budget the thread
    // we are waiting for is likely descheduled and spinning no longer helps.
    //
    // Use it wherever a thread waits for work another thread is about to finish in
    // a handful of instructions — the spinlock itself, the array's commit wait.
    class SpinBackoff
    {
    public:
        void Wait() noexcept
        {
            if (++mSpins >= kSpinsBeforeYield)
            {
                mSpins = 0;
                std::this_thread::yield();
            }
            else
            {
                PLU_CPU_PAUSE();
            }
        }

        void Reset() noexcept { mSpins = 0; }

    private:
        static constexpr int kSpinsBeforeYield = 64;
        int mSpins = 0;
    };

    // ========================================================================
    // SpinLock
    // ========================================================================
    // A test-and-test-and-set spinlock for *very* short critical sections — the
    // striped map's per-bucket locks, where the whole section is a handful of
    // pointer hops. Never use it around I/O, allocation-heavy work, or anything
    // that can block: a spinning waiter burns a core the whole time.
    //
    // Not recursive. Locking the same SpinLock twice on one thread deadlocks.
    class SpinLock
    {
    public:
        SpinLock() noexcept = default;

        SpinLock(const SpinLock&) = delete;
        SpinLock& operator=(const SpinLock&) = delete;

        void Lock() noexcept
        {
            SpinBackoff backoff;
            while (mFlag.exchange(true, std::memory_order_acquire))
            {
                // Test-and-test-and-set: poll with a plain load so the cache line stays
                // shared while we wait, instead of ping-ponging exclusive ownership.
                while (mFlag.load(std::memory_order_relaxed))
                    backoff.Wait();
            }
        }

        bool TryLock() noexcept
        {
            return !mFlag.load(std::memory_order_relaxed)
                && !mFlag.exchange(true, std::memory_order_acquire);
        }

        void Unlock() noexcept
        {
            mFlag.store(false, std::memory_order_release);
        }

        // Diagnostics only — the answer is stale the moment it is returned.
        [[nodiscard]] bool IsLockedApprox() const noexcept
        {
            return mFlag.load(std::memory_order_relaxed);
        }

    private:
        std::atomic<bool> mFlag{false};
    };

    // RAII guard for SpinLock.
    class ScopedSpinLock
    {
    public:
        explicit ScopedSpinLock(SpinLock& lock) noexcept : mLock(&lock) { mLock->Lock(); }
        ~ScopedSpinLock() noexcept { mLock->Unlock(); }

        ScopedSpinLock(const ScopedSpinLock&) = delete;
        ScopedSpinLock& operator=(const ScopedSpinLock&) = delete;

    private:
        SpinLock* mLock;
    };

    // A SpinLock padded out to its own cache line. Arrays of these are what "striping"
    // means: neighbouring stripes must not share a line or the striping buys nothing.
    struct alignas(kCacheLineSize) PaddedSpinLock
    {
        SpinLock Lock;
        // Fill the rest of the line. sizeof(SpinLock) is 1 in practice, but spell the
        // arithmetic out so the padding stays correct if that ever changes.
        char Padding[kCacheLineSize - sizeof(SpinLock)]{};
    };

    // ========================================================================
    // StripeArray<Count>
    // ========================================================================
    // The lock half of a striped container: Count padded spinlocks plus the two
    // ways they are ever taken — one stripe by hash, or all of them in index
    // order. Shared by ConcurrentHashMap and ConcurrentHashSet through
    // Detail::StripedHashTable.
    //
    // Locking every stripe always happens in ascending index order, and only ever
    // from a thread holding no stripe, which is what keeps the lock order global
    // and the containers deadlock-free.
    template<std::size_t Count>
    class StripeArray
    {
        static_assert(Count >= 1, "StripeArray needs at least one stripe");
        static_assert((Count & (Count - 1)) == 0, "StripeArray needs a power-of-two stripe count");

    public:
        static constexpr std::size_t kCount = Count;
        static constexpr std::size_t kMask = Count - 1;

        // The stripe guarding `hash`. Deliberately the LOW bits: a container whose bucket
        // count is a power of two >= Count can then pick the stripe without reading the
        // bucket array, i.e. before taking any lock.
        [[nodiscard]] SpinLock& Of(std::size_t hash) const noexcept { return mStripes[hash & kMask].Lock; }

        [[nodiscard]] SpinLock& At(std::size_t index) const noexcept { return mStripes[index].Lock; }

        void LockAll() const noexcept
        {
            for (std::size_t i = 0; i < Count; ++i)
                mStripes[i].Lock.Lock();
        }

        void UnlockAll() const noexcept
        {
            for (std::size_t i = Count; i > 0; --i)
                mStripes[i - 1].Lock.Unlock();
        }

        // RAII for "freeze the whole container" sections (Clear, Rehash, Drain).
        class ScopedAll
        {
        public:
            explicit ScopedAll(const StripeArray& stripes) noexcept : mStripes(&stripes) { mStripes->LockAll(); }
            ~ScopedAll() noexcept { mStripes->UnlockAll(); }

            ScopedAll(const ScopedAll&) = delete;
            ScopedAll& operator=(const ScopedAll&) = delete;

        private:
            const StripeArray* mStripes;
        };

    private:
        mutable PaddedSpinLock mStripes[Count];
    };
}

#endif //PLUSTL_LOCKPRIMITIVES_H
