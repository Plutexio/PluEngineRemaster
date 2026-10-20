//
// Created by Plutex on 2026-08-06.
//

#ifndef PLUSTL_CONCURRENTARRAY_H
#define PLUSTL_CONCURRENTARRAY_H

#include <atomic>
#include <cassert>
#include <cstddef>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>

#include "PluSTL/Concurrent/LockPrimitives.h"

namespace Plu
{
    // ========================================================================
    // ConcurrentArray<T, ChunkSize, MaxChunks> — append-only, stable addresses
    // ========================================================================
    //
    // The piece DynamicArray fundamentally cannot be: an element's address never
    // changes once it has been pushed. DynamicArray reallocates on Reserve and its
    // iterators are raw T*, so any pointer another thread holds dangles the moment
    // the array grows. Here growth allocates a *new chunk* and publishes it into a
    // fixed chunk table — existing chunks are never touched.
    //
    // The naming follows DynamicArray (PushBack / EmplaceBack / Reserve / Size /
    // Capacity / IsEmpty / Clear / Contains / IndexOf), with two differences forced
    // by the guarantees above:
    //
    //   * PushBack/EmplaceBack return the element's INDEX, not a reference —
    //     see Concurrent.h, rule 1. The index is the element's permanent address.
    //   * There is no Erase, no Insert-in-the-middle, no PopBack and no
    //     Find/FindIf returning an iterator: all of them would have to shuffle
    //     elements or hand out a T*, destroying the one guarantee that makes the
    //     container useful. IndexOf/IndexOfIf are the iterator-free searches; a
    //     slot map (EngineObjectManager) wants exactly this shape, with reuse
    //     being the free list's job rather than the array's.
    //
    // PushBack is lock-free except when it is the call that first touches a new
    // chunk (then it takes mGrowMutex for the allocation only). Get copies out;
    // Visit gives in-place access, which is safe precisely because the element
    // never moves.
    //
    // Thread-safety summary:
    //   PushBack/EmplaceBack — any number of threads.
    //   Get/Visit/Size/…     — any number of threads, concurrently with PushBack.
    //   Reserve              — any number of threads, concurrently with PushBack.
    //   Clear                — exclusive. No other operation may be in flight.
    //   Two threads mutating the SAME index (Visit/Visit, Visit/Get) is the
    //   caller's problem, exactly as it is for a plain array.
    template<typename T, std::size_t ChunkSize = 256, std::size_t MaxChunks = 1024>
    class ConcurrentArray
    {
        static_assert(ChunkSize >= 1, "ConcurrentArray needs a non-zero chunk size");
        static_assert(MaxChunks >= 1, "ConcurrentArray needs at least one chunk slot");

    public:
        using ValueType = T;
        using SizeType = std::size_t;

        static constexpr SizeType kChunkSize = ChunkSize;
        static constexpr SizeType kMaxChunks = MaxChunks;
        static constexpr SizeType kMaxSize = ChunkSize * MaxChunks;
        static constexpr SizeType InvalidIndex = static_cast<SizeType>(-1);

        ConcurrentArray() = default;

        ~ConcurrentArray() { Clear(); }

        ConcurrentArray(const ConcurrentArray&) = delete;
        ConcurrentArray& operator=(const ConcurrentArray&) = delete;
        ConcurrentArray(ConcurrentArray&&) = delete;
        ConcurrentArray& operator=(ConcurrentArray&&) = delete;

        // ====================================================================
        // WRITES
        // ====================================================================

        // Appends and returns the element's index — its permanent address for the rest of
        // the array's life. InvalidIndex when the chunk table is exhausted (MaxCapacity()).
        SizeType PushBack(const T& value)
        {
            return EmplaceInternal(value);
        }

        SizeType PushBack(T&& value)
        {
            return EmplaceInternal(static_cast<T&&>(value));
        }

        template<typename... Args>
        SizeType EmplaceBack(Args&&... args)
        {
            return EmplaceInternal(static_cast<Args&&>(args)...);
        }

        // Allocates the chunks needed to hold `capacity` elements, so the pushes that would
        // have touched a fresh chunk do not pay for the allocation (nor take mGrowMutex)
        // under contention. Never shrinks; capped at MaxCapacity().
        void Reserve(SizeType capacity)
        {
            if (capacity > kMaxSize) capacity = kMaxSize;
            if (capacity <= Capacity()) return;

            const SizeType chunksNeeded = (capacity + ChunkSize - 1) / ChunkSize;

            std::lock_guard<std::mutex> guard(mGrowMutex);
            for (SizeType chunk = 0; chunk < chunksNeeded; ++chunk)
                AllocateChunkUnderGrowMutex(chunk);
        }

        // ====================================================================
        // READS
        // ====================================================================

        // Copies the element out. False when the index is not (yet) published.
        // The copy-out stand-in for DynamicArray's operator[] / At.
        bool Get(SizeType index, T& outValue) const
        {
            if (index >= Size()) return false;
            outValue = *SlotAt(index);
            return true;
        }

        // False when the array is empty. Copy-out stand-ins for Front()/Back().
        bool Front(T& outValue) const { return Get(0, outValue); }

        bool Back(T& outValue) const
        {
            const SizeType size = Size();
            return size > 0 && Get(size - 1, outValue);
        }

        // In-place access: fn(T&). Safe without any lock because the element never moves —
        // but see the thread-safety note above about two writers on one index.
        template<typename Fn>
        bool Visit(SizeType index, Fn&& fn)
        {
            if (index >= Size()) return false;
            fn(*SlotAt(index));
            return true;
        }

        template<typename Fn>
        bool Visit(SizeType index, Fn&& fn) const
        {
            if (index >= Size()) return false;
            fn(static_cast<const T&>(*SlotAt(index)));
            return true;
        }

        // fn(SizeType index, T&) over everything published when the walk started. Elements
        // pushed during the walk are not visited; nothing is ever skipped or repeated.
        template<typename Fn>
        void ForEach(Fn&& fn)
        {
            const SizeType size = Size();
            for (SizeType i = 0; i < size; ++i)
                fn(i, *SlotAt(i));
        }

        template<typename Fn>
        void ForEach(Fn&& fn) const
        {
            const SizeType size = Size();
            for (SizeType i = 0; i < size; ++i)
                fn(i, static_cast<const T&>(*SlotAt(i)));
        }

        // ====================================================================
        // SEARCH — by index, never by iterator
        // ====================================================================

        // The index of the first equal element, or InvalidIndex — same contract as
        // DynamicArray::IndexOf, which returns SizeType(-1) on a miss.
        [[nodiscard]] SizeType IndexOf(const T& value) const
        {
            const SizeType size = Size();
            for (SizeType i = 0; i < size; ++i)
            {
                if (*SlotAt(i) == value) return i;
            }
            return InvalidIndex;
        }

        // DynamicArray::FindIf without the iterator: fn(const T&) -> bool.
        template<typename Predicate>
        [[nodiscard]] SizeType IndexOfIf(Predicate pred) const
        {
            const SizeType size = Size();
            for (SizeType i = 0; i < size; ++i)
            {
                if (pred(static_cast<const T&>(*SlotAt(i)))) return i;
            }
            return InvalidIndex;
        }

        [[nodiscard]] bool Contains(const T& value) const { return IndexOf(value) != InvalidIndex; }

        // ====================================================================
        // OBSERVERS
        // ====================================================================

        // Number of published elements — an index below this is safe to Get/Visit forever.
        [[nodiscard]] SizeType Size() const noexcept { return mCommitted.load(std::memory_order_acquire); }
        [[nodiscard]] bool IsEmpty() const noexcept { return Size() == 0; }

        // Elements that fit without allocating another chunk, i.e. DynamicArray::Capacity().
        [[nodiscard]] SizeType Capacity() const noexcept
        {
            return mAllocatedChunks.load(std::memory_order_acquire) * ChunkSize;
        }

        // The hard ceiling the chunk table imposes — raise MaxChunks to lift it.
        [[nodiscard]] static constexpr SizeType MaxCapacity() noexcept { return kMaxSize; }

        // Destroys every element and releases every chunk. NOT concurrency-safe: no
        // PushBack, Get, Visit or ForEach may be running on another thread. Every index
        // handed out so far becomes meaningless.
        void Clear()
        {
            std::lock_guard<std::mutex> guard(mGrowMutex);

            const SizeType size = mCommitted.load(std::memory_order_acquire);
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                for (SizeType i = 0; i < size; ++i)
                    SlotAt(i)->~T();
            }

            for (SizeType chunk = 0; chunk < kMaxChunks; ++chunk)
            {
                T* data = mChunks[chunk].load(std::memory_order_relaxed);
                if (!data) continue;
                ::operator delete(static_cast<void*>(data), ChunkSize * sizeof(T), std::align_val_t{alignof(T)});
                mChunks[chunk].store(nullptr, std::memory_order_relaxed);
            }

            mCommitted.store(0, std::memory_order_release);
            mReserved.store(0, std::memory_order_release);
            mAllocatedChunks.store(0, std::memory_order_release);
        }

    private:
        // Chunk table. Entries are published with release / read with acquire, so a reader
        // that sees the pointer also sees the storage behind it.
        std::atomic<T*> mChunks[MaxChunks]{};

        // Next index to hand out. Ahead of mCommitted while a push is mid-construction.
        alignas(kCacheLineSize) std::atomic<SizeType> mReserved{0};
        // Length of the fully-constructed prefix — what Size() reports.
        alignas(kCacheLineSize) std::atomic<SizeType> mCommitted{0};
        // How many chunks currently hold storage — what Capacity() reports. Only ever
        // written under mGrowMutex.
        alignas(kCacheLineSize) std::atomic<SizeType> mAllocatedChunks{0};

        std::mutex mGrowMutex;

        T* SlotAt(SizeType index) const noexcept
        {
            T* chunk = mChunks[index / ChunkSize].load(std::memory_order_acquire);
            return chunk + (index % ChunkSize);
        }

        // Caller must hold mGrowMutex. No-op when the chunk already exists.
        T* AllocateChunkUnderGrowMutex(SizeType chunkIndex)
        {
            T* chunk = mChunks[chunkIndex].load(std::memory_order_relaxed);
            if (chunk) return chunk;

            chunk = static_cast<T*>(::operator new(ChunkSize * sizeof(T), std::align_val_t{alignof(T)}));
            mChunks[chunkIndex].store(chunk, std::memory_order_release);
            mAllocatedChunks.fetch_add(1, std::memory_order_release);
            return chunk;
        }

        // Returns the raw (uninitialized) slot for `index`, allocating its chunk on the way
        // if this is the first element to land there.
        T* AcquireSlot(SizeType index)
        {
            const SizeType chunkIndex = index / ChunkSize;

            T* chunk = mChunks[chunkIndex].load(std::memory_order_acquire);
            if (!chunk)
            {
                std::lock_guard<std::mutex> guard(mGrowMutex);
                chunk = AllocateChunkUnderGrowMutex(chunkIndex);
            }
            return chunk + (index % ChunkSize);
        }

        // Publishes `index` once its element is constructed. Reservations can complete out
        // of order (thread B finishes index 6 before thread A finishes index 5), and Size()
        // is a prefix length, so a completing push waits for its predecessors. The wait is
        // bounded by one element construction, not by anything the caller controls.
        void CommitIndex(SizeType index)
        {
            SpinBackoff backoff;

            SizeType expected = index;
            while (!mCommitted.compare_exchange_weak(expected, index + 1,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed))
            {
                expected = index;
                backoff.Wait();
            }
        }

        template<typename... Args>
        SizeType EmplaceInternal(Args&&... args)
        {
            const SizeType index = mReserved.fetch_add(1, std::memory_order_relaxed);
            if (index >= kMaxSize)
            {
                // Undo the reservation so the counter cannot run away, then report failure.
                mReserved.fetch_sub(1, std::memory_order_relaxed);
                assert(false && "ConcurrentArray chunk table exhausted — raise MaxChunks");
                return InvalidIndex;
            }

            T* slot = AcquireSlot(index);
            new (slot) T(static_cast<Args&&>(args)...);
            CommitIndex(index);
            return index;
        }
    };
}

#endif //PLUSTL_CONCURRENTARRAY_H
