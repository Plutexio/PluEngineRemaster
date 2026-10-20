//
// Created by Plutex on 2026-08-07.
//

#ifndef PLUSTL_QUEUE_H
#define PLUSTL_QUEUE_H

#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <cstdlib>
#include "PluSTL/Allocators/Default.h"

namespace Plu
{
    // ========================================================================
    // Queue<T> — FIFO over a ring buffer
    // ========================================================================
    //
    // What DynamicArray cannot do cheaply: take from the front. `PopFront` on a
    // DynamicArray means shifting every remaining element down (O(n)); here the
    // head index simply moves, so push and pop are both O(1) and the storage is
    // reused as the queue walks around it.
    //
    // The API is DynamicArray's wherever the operation exists on both —
    // PushBack / EmplaceBack / Front / Back / operator[] / At / Size / Capacity /
    // IsEmpty / Reserve / Clear / ShrinkToFit / Swap / Find / FindIf / Contains /
    // IndexOf — so moving a member from one to the other is a type change. The
    // difference is which end you take from: `PopFront`, not `PopBack`. Indices
    // and iterators are **logical**: index 0 is the front, whatever the buffer is
    // doing underneath.
    //
    // Capacity is exact (no power-of-two rounding); the wrap is one conditional
    // subtract rather than a modulo. Growth doubles, starting at 4.
    //
    // Not thread-safe — that is ConcurrentQueue, which is this plus a mutex.
    template<typename T, typename Allocator = DefaultAllocator<T>>
    class Queue
    {
    public:
        using ValueType = T;
        using SizeType = std::size_t;

        static constexpr SizeType InvalidIndex = static_cast<SizeType>(-1);

        // ====================================================================
        // ITERATORS — logical order, front first
        // ====================================================================
        // Not raw pointers (the elements wrap), so they are a small class over
        // "queue + logical index". Any push may reallocate and invalidate them,
        // exactly as on DynamicArray.
        template<bool IsConst>
        class IteratorBase
        {
            using QueuePointer = std::conditional_t<IsConst, const Queue*, Queue*>;

        public:
            using Reference = std::conditional_t<IsConst, const T&, T&>;
            using Pointer = std::conditional_t<IsConst, const T*, T*>;

            IteratorBase() noexcept : mQueue(nullptr), mIndex(0) {}
            IteratorBase(QueuePointer queue, SizeType index) noexcept : mQueue(queue), mIndex(index) {}

            // Non-const -> const conversion, so Begin() of a mutable queue compares
            // against ConstIterator.
            template<bool OtherConst, typename = std::enable_if_t<IsConst && !OtherConst>>
            IteratorBase(const IteratorBase<OtherConst>& other) noexcept
                : mQueue(other.mQueue), mIndex(other.mIndex) {}

            Reference operator*() const noexcept { return (*mQueue)[mIndex]; }
            Pointer operator->() const noexcept { return &(*mQueue)[mIndex]; }

            IteratorBase& operator++() noexcept { ++mIndex; return *this; }
            IteratorBase operator++(int) noexcept { IteratorBase tmp = *this; ++mIndex; return tmp; }
            IteratorBase& operator--() noexcept { --mIndex; return *this; }
            IteratorBase operator--(int) noexcept { IteratorBase tmp = *this; --mIndex; return tmp; }

            IteratorBase operator+(SizeType offset) const noexcept { return IteratorBase(mQueue, mIndex + offset); }
            IteratorBase operator-(SizeType offset) const noexcept { return IteratorBase(mQueue, mIndex - offset); }

            std::ptrdiff_t operator-(const IteratorBase& other) const noexcept
            {
                return static_cast<std::ptrdiff_t>(mIndex) - static_cast<std::ptrdiff_t>(other.mIndex);
            }

            bool operator==(const IteratorBase& other) const noexcept { return mIndex == other.mIndex && mQueue == other.mQueue; }
            bool operator!=(const IteratorBase& other) const noexcept { return !(*this == other); }
            bool operator<(const IteratorBase& other) const noexcept { return mIndex < other.mIndex; }
            bool operator>(const IteratorBase& other) const noexcept { return mIndex > other.mIndex; }

        private:
            friend class Queue;
            template<bool> friend class IteratorBase;

            QueuePointer mQueue;
            SizeType mIndex;
        };

        using Iterator = IteratorBase<false>;
        using ConstIterator = IteratorBase<true>;

        // ====================================================================
        // CONSTRUCTION
        // ====================================================================

        explicit Queue(const Allocator& alloc = Allocator())
            : mData(nullptr), mCapacity(0), mHead(0), mSize(0), mAllocator(alloc) {}

        explicit Queue(SizeType capacity, const Allocator& alloc = Allocator())
            : mData(nullptr), mCapacity(0), mHead(0), mSize(0), mAllocator(alloc)
        {
            Reserve(capacity);
        }

        Queue(std::initializer_list<T> init, const Allocator& alloc = Allocator())
            : mData(nullptr), mCapacity(0), mHead(0), mSize(0), mAllocator(alloc)
        {
            Reserve(init.size());
            for (const T& item : init) PushBack(item);
        }

        Queue(const Queue& other)
            : mData(nullptr), mCapacity(0), mHead(0), mSize(0), mAllocator(other.mAllocator)
        {
            Reserve(other.mSize);
            for (SizeType i = 0; i < other.mSize; ++i)
                mAllocator.Construct(&mData[i], other[i]);
            mSize = other.mSize;
        }

        Queue(Queue&& other) noexcept
            : mData(other.mData), mCapacity(other.mCapacity), mHead(other.mHead), mSize(other.mSize)
            , mAllocator(std::move(other.mAllocator))
        {
            other.mData = nullptr;
            other.mCapacity = 0;
            other.mHead = 0;
            other.mSize = 0;
        }

        ~Queue()
        {
            Clear();
            if (mData) mAllocator.Deallocate(mData, mCapacity);
        }

        Queue& operator=(const Queue& other)
        {
            if (this != &other)
            {
                Clear();
                Reserve(other.mSize);
                for (SizeType i = 0; i < other.mSize; ++i)
                    mAllocator.Construct(&mData[i], other[i]);
                mHead = 0;
                mSize = other.mSize;
            }
            return *this;
        }

        Queue& operator=(Queue&& other) noexcept
        {
            if (this != &other)
            {
                Clear();
                if (mData) mAllocator.Deallocate(mData, mCapacity);

                mData = other.mData;
                mCapacity = other.mCapacity;
                mHead = other.mHead;
                mSize = other.mSize;
                mAllocator = std::move(other.mAllocator);

                other.mData = nullptr;
                other.mCapacity = 0;
                other.mHead = 0;
                other.mSize = 0;
            }
            return *this;
        }

        // ====================================================================
        // WRITES
        // ====================================================================

        void PushBack(const T& value)
        {
            if (!GrowIfFull()) return;
            mAllocator.Construct(&mData[PhysicalIndex(mSize)], value);
            ++mSize;
        }

        void PushBack(T&& value)
        {
            if (!GrowIfFull()) return;
            mAllocator.Construct(&mData[PhysicalIndex(mSize)], std::move(value));
            ++mSize;
        }

        template<typename... Args>
        T& EmplaceBack(Args&&... args)
        {
            // Owes the caller a reference, so there is no element to silently drop.
            if (!GrowIfFull()) std::abort();
            T* slot = &mData[PhysicalIndex(mSize)];
            mAllocator.Construct(slot, std::forward<Args>(args)...);
            ++mSize;
            return *slot;
        }

        // Drops the front element. A no-op on an empty queue, like DynamicArray::PopBack.
        void PopFront()
        {
            if (mSize == 0) return;
            mAllocator.Destroy(&mData[mHead]);
            mHead = Wrap(mHead + 1);
            --mSize;
        }

        // Moves the front element into `out` and drops it. False when the queue is empty
        // (`out` untouched) — the "take one item of work" loop:
        //     T item; while (queue.TryPopFront(item)) Process(item);
        bool TryPopFront(T& out)
        {
            if (mSize == 0) return false;
            out = std::move(mData[mHead]);
            PopFront();
            return true;
        }

        // ====================================================================
        // ELEMENT ACCESS — index 0 is the front
        // ====================================================================

        T& operator[](SizeType index) { return mData[PhysicalIndex(index)]; }
        const T& operator[](SizeType index) const { return mData[PhysicalIndex(index)]; }

        T& At(SizeType index)
        {
            if (index >= mSize) throw std::out_of_range("Index out of range");
            return mData[PhysicalIndex(index)];
        }

        const T& At(SizeType index) const
        {
            if (index >= mSize) throw std::out_of_range("Index out of range");
            return mData[PhysicalIndex(index)];
        }

        T& Front() { return mData[mHead]; }
        const T& Front() const { return mData[mHead]; }
        T& Back() { return mData[PhysicalIndex(mSize - 1)]; }
        const T& Back() const { return mData[PhysicalIndex(mSize - 1)]; }

        // ====================================================================
        // CAPACITY
        // ====================================================================

        [[nodiscard]] SizeType Size() const noexcept { return mSize; }
        [[nodiscard]] SizeType Capacity() const noexcept { return mCapacity; }
        [[nodiscard]] bool IsEmpty() const noexcept { return mSize == 0; }

        // Grows only. Elements are re-laid-out from index 0, so the ring unwraps.
        void Reserve(SizeType newCapacity)
        {
            if (newCapacity <= mCapacity) return;
            Reallocate(newCapacity);
        }

        void ShrinkToFit()
        {
            if (mSize < mCapacity) Reallocate(mSize);
        }

        void Clear()
        {
            for (SizeType i = 0; i < mSize; ++i)
                mAllocator.Destroy(&mData[PhysicalIndex(i)]);
            mHead = 0;
            mSize = 0;
        }

        // O(1) — this is what makes a "hand the whole batch over" drain cheap.
        void Swap(Queue& other) noexcept
        {
            T* data = mData; mData = other.mData; other.mData = data;
            SizeType capacity = mCapacity; mCapacity = other.mCapacity; other.mCapacity = capacity;
            SizeType head = mHead; mHead = other.mHead; other.mHead = head;
            SizeType size = mSize; mSize = other.mSize; other.mSize = size;
        }

        // ====================================================================
        // ITERATION & SEARCH
        // ====================================================================

        Iterator Begin() noexcept { return Iterator(this, 0); }
        ConstIterator Begin() const noexcept { return ConstIterator(this, 0); }
        Iterator End() noexcept { return Iterator(this, mSize); }
        ConstIterator End() const noexcept { return ConstIterator(this, mSize); }

        Iterator begin() noexcept { return Begin(); }
        ConstIterator begin() const noexcept { return Begin(); }
        Iterator end() noexcept { return End(); }
        ConstIterator end() const noexcept { return End(); }

        Iterator Find(const T& value) noexcept
        {
            for (SizeType i = 0; i < mSize; ++i)
            {
                if ((*this)[i] == value) return Iterator(this, i);
            }
            return End();
        }

        ConstIterator Find(const T& value) const noexcept
        {
            for (SizeType i = 0; i < mSize; ++i)
            {
                if ((*this)[i] == value) return ConstIterator(this, i);
            }
            return End();
        }

        template<typename Predicate>
        Iterator FindIf(Predicate pred)
        {
            for (SizeType i = 0; i < mSize; ++i)
            {
                if (pred((*this)[i])) return Iterator(this, i);
            }
            return End();
        }

        template<typename Predicate>
        ConstIterator FindIf(Predicate pred) const
        {
            for (SizeType i = 0; i < mSize; ++i)
            {
                if (pred((*this)[i])) return ConstIterator(this, i);
            }
            return End();
        }

        [[nodiscard]] bool Contains(const T& value) const { return Find(value) != End(); }

        // InvalidIndex (== SizeType(-1)) on a miss, like DynamicArray::IndexOf.
        [[nodiscard]] SizeType IndexOf(const T& value) const
        {
            for (SizeType i = 0; i < mSize; ++i)
            {
                if ((*this)[i] == value) return i;
            }
            return InvalidIndex;
        }

    private:
        T* mData;
        SizeType mCapacity;
        SizeType mHead;    // physical index of the front element
        SizeType mSize;
        Allocator mAllocator;

        // logical -> physical. `logical` is < mCapacity, so mHead + logical is below
        // 2 * mCapacity and one conditional subtract replaces the modulo.
        [[nodiscard]] SizeType PhysicalIndex(SizeType logical) const noexcept
        {
            return Wrap(mHead + logical);
        }

        [[nodiscard]] SizeType Wrap(SizeType index) const noexcept
        {
            return index >= mCapacity ? index - mCapacity : index;
        }

        // Makes room for one more element. false = the allocator refused, and the
        // caller must drop the push rather than construct past the end.
        bool GrowIfFull()
        {
            if (mSize < mCapacity) return true;
            Reallocate(mCapacity == 0 ? 4 : mCapacity * 2);
            return mSize < mCapacity;
        }

        // Moves everything into a fresh block in logical order, so the queue comes out
        // unwrapped with mHead == 0.
        void Reallocate(SizeType newCapacity)
        {
            if (newCapacity < mSize) newCapacity = mSize;

            T* newData = newCapacity > 0 ? mAllocator.Allocate(newCapacity) : nullptr;
            // Allocate is failable — keep the current block rather than move into null.
            if (newCapacity > 0 && !newData) return;

            for (SizeType i = 0; i < mSize; ++i)
            {
                T* source = &mData[PhysicalIndex(i)];
                mAllocator.Construct(&newData[i], std::move(*source));
                mAllocator.Destroy(source);
            }

            if (mData) mAllocator.Deallocate(mData, mCapacity);

            mData = newData;
            mCapacity = newCapacity;
            mHead = 0;
        }
    };
}

#endif //PLUSTL_QUEUE_H
