//
// Created by Plutex on 2026-08-06.
//

#ifndef PLUSTL_CONCURRENTQUEUE_H
#define PLUSTL_CONCURRENTQUEUE_H

#include <atomic>
#include <cstddef>
#include <mutex>
#include <utility>

#include "PluSTL/Queue/Queue.h"
#include "PluSTL/Concurrent/LockPrimitives.h"

namespace Plu
{
    // ========================================================================
    // ConcurrentQueue<T> — Queue, guarded; many producers, one consumer
    // ========================================================================
    //
    // A mutex plus a Queue, and it keeps that Queue's API: PushBack / EmplaceBack /
    // TryPopFront / Size / IsEmpty / Capacity / Reserve / Contains / Clear mean
    // exactly what they mean there. What it drops is what rule 1 forbids —
    // Front()/Back()/operator[]/iterators hand out a reference into the storage —
    // so reads happen through TryPopFront (copy out) or Drain.
    //
    // Drain() is the addition that matters more than the locking: it swaps the
    // whole buffer out in O(1) instead of popping element by element, so the lock
    // is held for a constant time no matter how deep the queue got, and the
    // consumer walks the batch with no lock held at all.
    //
    //     Queue<Request> scratch;          // a LOCAL, never a member — see below
    //     queue.Drain(scratch);
    //     for (Request& r : scratch) Process(r);   // no lock held here
    //
    // The scratch buffer must be a local of the draining function. A member would
    // be re-entered if Process() itself drains, and would keep the batch alive past
    // the point the consumer thinks it released it.
    //
    // Ordering is FIFO, per element and per batch: a drain returns everything
    // pushed so far, in push order.
    template<typename T>
    class ConcurrentQueue
    {
    public:
        using ValueType = T;
        using SizeType = std::size_t;

        ConcurrentQueue() = default;

        ConcurrentQueue(const ConcurrentQueue&) = delete;
        ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;
        ConcurrentQueue(ConcurrentQueue&&) = delete;
        ConcurrentQueue& operator=(ConcurrentQueue&&) = delete;

        void PushBack(const T& value)
        {
            std::lock_guard<std::mutex> guard(mMutex);
            mBuffer.PushBack(value);
            mSize.store(mBuffer.Size(), std::memory_order_relaxed);
        }

        void PushBack(T&& value)
        {
            std::lock_guard<std::mutex> guard(mMutex);
            mBuffer.PushBack(static_cast<T&&>(value));
            mSize.store(mBuffer.Size(), std::memory_order_relaxed);
        }

        template<typename... Args>
        void EmplaceBack(Args&&... args)
        {
            std::lock_guard<std::mutex> guard(mMutex);
            mBuffer.EmplaceBack(static_cast<Args&&>(args)...);
            mSize.store(mBuffer.Size(), std::memory_order_relaxed);
        }

        // Pushes only when an equal value is not already queued. True = it was added.
        // The scan covers the pending batch only (which is what "already queued" means) —
        // it is O(n) in the queue depth, so keep this for the "re-request every frame until
        // it lands" pattern where the depth is small by construction.
        bool PushBackUnique(const T& value)
        {
            std::lock_guard<std::mutex> guard(mMutex);
            for (const T& queued : mBuffer)
            {
                if (queued == value) return false;
            }
            mBuffer.PushBack(value);
            mSize.store(mBuffer.Size(), std::memory_order_relaxed);
            return true;
        }

        // Same, with a caller-supplied identity test — for element types where operator==
        // is not the right notion of "the same request" (a TUsePointer compares the pointer,
        // not the UUID behind it). fn(const T& queued) -> bool "this is the same request".
        template<typename Fn>
        bool PushBackUniqueIf(const T& value, Fn&& isSame)
        {
            std::lock_guard<std::mutex> guard(mMutex);
            for (const T& queued : mBuffer)
            {
                if (isSame(static_cast<const T&>(queued))) return false;
            }
            mBuffer.PushBack(value);
            mSize.store(mBuffer.Size(), std::memory_order_relaxed);
            return true;
        }

        // Copies the front element out and drops it — Queue::TryPopFront, minus the
        // reference. False when the queue is empty (`out` untouched). For one item at a
        // time; prefer Drain when there is a whole batch to process, since this takes the
        // lock per element.
        bool TryPopFront(T& out)
        {
            std::lock_guard<std::mutex> guard(mMutex);
            if (!mBuffer.TryPopFront(out)) return false;
            mSize.store(mBuffer.Size(), std::memory_order_relaxed);
            return true;
        }

        // Moves everything queued into `out` in O(1) and leaves the queue empty. Anything
        // `out` held before the call is discarded. `out`'s old storage is recycled as the
        // queue's next write buffer, so a consumer reusing the same local stops allocating
        // after the first few frames.
        void Drain(Queue<T>& out)
        {
            out.Clear();
            {
                std::lock_guard<std::mutex> guard(mMutex);
                mBuffer.Swap(out);
                mSize.store(0, std::memory_order_relaxed);
            }
        }

        void Clear()
        {
            std::lock_guard<std::mutex> guard(mMutex);
            mBuffer.Clear();
            mSize.store(0, std::memory_order_relaxed);
        }

        // Pre-grows the write buffer so the first pushes of a burst do not reallocate while
        // the lock is held.
        void Reserve(SizeType capacity)
        {
            std::lock_guard<std::mutex> guard(mMutex);
            mBuffer.Reserve(capacity);
        }

        // Is an equal value in the pending batch? Same O(n) caveat as PushBackUnique, and
        // the answer is stale as soon as it is returned.
        [[nodiscard]] bool Contains(const T& value) const
        {
            std::lock_guard<std::mutex> guard(mMutex);
            for (const T& queued : mBuffer)
            {
                if (queued == value) return true;
            }
            return false;
        }

        // Stale the moment they are returned — for telemetry and "is there anything to do at
        // all" fast-outs, never for control flow that assumes the answer stays true. (The
        // same is true of Size() on every other container here; it is not a weaker promise.)
        [[nodiscard]] SizeType Size() const noexcept { return mSize.load(std::memory_order_relaxed); }
        [[nodiscard]] bool IsEmpty() const noexcept { return Size() == 0; }

        [[nodiscard]] SizeType Capacity() const
        {
            std::lock_guard<std::mutex> guard(mMutex);
            return mBuffer.Capacity();
        }

    private:
        mutable std::mutex mMutex;
        Queue<T> mBuffer;
        std::atomic<SizeType> mSize{0};
    };

    // ========================================================================
    // ConcurrentRingQueue<T, SlotCount> — bounded, lock-free, single producer,
    // single consumer
    // ========================================================================
    //
    // For small fixed-volume handoffs where a mutex would be all of the cost (the
    // window-lifecycle queues: "this window needs its GL backend", "this window is
    // safe to destroy"). SlotCount must be a power of two; one slot is kept free to
    // tell "full" from "empty", so the usable depth — what Capacity() reports — is
    // SlotCount - 1.
    //
    // Exactly ONE producer thread may call TryPushBack and exactly ONE consumer
    // thread may call TryPopFront. Two producers corrupt it — use ConcurrentQueue
    // for that.
    //
    // Being bounded, the push can fail, so this is the one container here where the
    // write is spelled TryPushBack rather than PushBack. Everything else (Size /
    // IsEmpty / Capacity / IsFull) reads as it does on Queue.
    template<typename T, std::size_t SlotCount = 64>
    class ConcurrentRingQueue
    {
        static_assert(SlotCount >= 2, "ConcurrentRingQueue needs at least 2 slots");
        static_assert((SlotCount & (SlotCount - 1)) == 0, "ConcurrentRingQueue slot count must be a power of two");

    public:
        using ValueType = T;
        using SizeType = std::size_t;

        static constexpr SizeType kSlotCount = SlotCount;
        static constexpr SizeType kMask = SlotCount - 1;
        // Usable depth — one slot is always kept empty as the full/empty discriminator.
        static constexpr SizeType kMaxDepth = SlotCount - 1;

        ConcurrentRingQueue() = default;

        ConcurrentRingQueue(const ConcurrentRingQueue&) = delete;
        ConcurrentRingQueue& operator=(const ConcurrentRingQueue&) = delete;
        ConcurrentRingQueue(ConcurrentRingQueue&&) = delete;
        ConcurrentRingQueue& operator=(ConcurrentRingQueue&&) = delete;

        // Producer thread only. False when the ring is full — the caller decides whether to
        // retry, drop, or grow a spill buffer.
        bool TryPushBack(const T& value)
        {
            const SizeType tail = mTail.load(std::memory_order_relaxed);
            const SizeType next = (tail + 1) & kMask;
            if (next == mHead.load(std::memory_order_acquire)) return false;

            mSlots[tail] = value;
            mTail.store(next, std::memory_order_release);
            return true;
        }

        bool TryPushBack(T&& value)
        {
            const SizeType tail = mTail.load(std::memory_order_relaxed);
            const SizeType next = (tail + 1) & kMask;
            if (next == mHead.load(std::memory_order_acquire)) return false;

            mSlots[tail] = static_cast<T&&>(value);
            mTail.store(next, std::memory_order_release);
            return true;
        }

        // Consumer thread only. False when the ring is empty (outValue untouched).
        bool TryPopFront(T& outValue)
        {
            const SizeType head = mHead.load(std::memory_order_relaxed);
            if (head == mTail.load(std::memory_order_acquire)) return false;

            outValue = static_cast<T&&>(mSlots[head]);
            mSlots[head] = T{}; // drop whatever the slot still owns (String buffer, TUsePointer)
            mHead.store((head + 1) & kMask, std::memory_order_release);
            return true;
        }

        // Stale the moment they are returned, exactly like ConcurrentQueue's.
        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return mHead.load(std::memory_order_acquire) == mTail.load(std::memory_order_acquire);
        }

        [[nodiscard]] SizeType Size() const noexcept
        {
            const SizeType head = mHead.load(std::memory_order_acquire);
            const SizeType tail = mTail.load(std::memory_order_acquire);
            return (tail - head) & kMask;
        }

        [[nodiscard]] bool IsFull() const noexcept { return Size() == kMaxDepth; }

        // How many elements fit — the usable depth, not the slot count.
        [[nodiscard]] static constexpr SizeType Capacity() noexcept { return kMaxDepth; }

    private:
        T mSlots[SlotCount]{};

        // Head and tail are written by different threads — keep them in different cache
        // lines or every push invalidates the consumer's line and vice versa.
        alignas(kCacheLineSize) std::atomic<SizeType> mHead{0};
        alignas(kCacheLineSize) std::atomic<SizeType> mTail{0};
    };
}

#endif //PLUSTL_CONCURRENTQUEUE_H
