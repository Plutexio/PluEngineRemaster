//
// Created by Plutex on 1/12/26.
//

#ifndef PLUSTL_HASHMAP_H
#define PLUSTL_HASHMAP_H

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include "PluSTL/Allocators/Default.h"
#include "PluSTL/Hashers/Default.h"

namespace Plu
{
    // =========================================================================
    // HASH MAP
    // =========================================================================
    // Separate chaining: an array of bucket heads, one heap node per element.
    //
    // Why chaining and not open addressing: Find() and operator[] hand back a
    // TValue*, and callers across the engine hold on to it. With chaining a node
    // keeps its address from insertion until that key is removed — a rehash only
    // relinks Next pointers, it never moves a node. Open addressing would give
    // better cache locality but would invalidate every such pointer on growth.
    //
    // The per-insert malloc that chaining normally costs is gone: removed and
    // cleared nodes go onto a free list and are reused. A map that is Clear()ed
    // and refilled every frame — RenderSnapshotBuilder's batch lookup, frame use
    // counters — allocates during the first frames and then never again.
    template
    <
        typename TKey,
        typename TValue,
        typename THasher = DefaultHash<TKey>,
        typename TAllocator = DefaultAllocator<std::pair<TKey, TValue>>
    >
    class HashMap
    {
    public:
        using KeyType = TKey;
        using MappedType = TValue;
        using ValueType = std::pair<TKey, TValue>;
        using SizeType = std::size_t;
        using HasherType = THasher;
        using AllocatorType = TAllocator;

    private:
        // Raw storage plus a link, deliberately trivial: a node on the free list has
        // no live element in it, yet its Next still has to be written. Giving Node a
        // constructor would mean writing through a pointer whose object lifetime has
        // ended. Same shape as HashSet::Slot.
        struct Node
        {
            alignas(ValueType) unsigned char Storage[sizeof(ValueType)];
            Node* Next;

            [[nodiscard]] ValueType* Value() noexcept { return reinterpret_cast<ValueType*>(Storage); }
            [[nodiscard]] const ValueType* Value() const noexcept { return reinterpret_cast<const ValueType*>(Storage); }
        };

        // The caller's allocator, re-targeted at what this container actually
        // allocates. Before rebinding existed, TAllocator was stored and never used
        // while nodes went through a hardcoded DefaultAllocator and buckets through
        // raw ::operator new — a custom allocator was silently dropped.
        using NodeAllocator = RebindAllocatorT<TAllocator, Node>;       // node storage
        using BucketAllocator = RebindAllocatorT<TAllocator, Node*>;    // bucket heads
        using ValueAllocator = RebindAllocatorT<TAllocator, ValueType>; // the pair itself

        Node** mBuckets = nullptr;
        SizeType mBucketCount = 0;
        SizeType mElementCount = 0;
        Node* mFreeList = nullptr;
        SizeType mFreeCount = 0;
        [[no_unique_address]] HasherType mHasher{};
        [[no_unique_address]] NodeAllocator mNodeAllocator{};
        [[no_unique_address]] BucketAllocator mBucketAllocator{};
        [[no_unique_address]] ValueAllocator mValueAllocator{};

        static constexpr float kMaxLoadFactor = 0.75f;
        static constexpr SizeType kMinBucketCount = 8;

        [[nodiscard]] static SizeType NextPowerOfTwo(SizeType n) noexcept
        {
            if (n <= kMinBucketCount) return kMinBucketCount;
            --n;
            n |= n >> 1;
            n |= n >> 2;
            n |= n >> 4;
            n |= n >> 8;
            n |= n >> 16;
            n |= n >> 32;
            return n + 1;
        }

        // Bucket count is always a power of two, so the index is a mask, never a
        // division. (The old map used `% BucketCount` even though the count was a
        // power of two everywhere except Reserve, which produced Count/0.75 + 1.)
        [[nodiscard]] SizeType BucketIndexFor(const TKey& key) const noexcept
        {
            return mHasher(key) & (mBucketCount - 1);
        }

        // A default-constructed or moved-from map owns no buckets. Every mutating
        // path funnels through here first: without it BucketIndexFor would mask
        // against (0 - 1) on a fresh map, and the previous `% mBucketCount` divided
        // by zero outright.
        bool EnsureBuckets()
        {
            if (mBuckets) return true;
            return ReallocateBuckets(kMinBucketCount);
        }

        bool ReallocateBuckets(SizeType newBucketCount)
        {
            Node** newBuckets = mBucketAllocator.Allocate(newBucketCount);
            if (!newBuckets) return false; // Out of memory: keep the map as it was.

            for (SizeType i = 0; i < newBucketCount; ++i)
                newBuckets[i] = nullptr;

            // Relink every existing node into the new bucket array. Nodes keep their
            // addresses, which is what makes an outstanding TValue* survive a rehash.
            for (SizeType i = 0; i < mBucketCount; ++i)
            {
                Node* current = mBuckets[i];
                while (current)
                {
                    Node* next = current->Next;
                    const SizeType index = mHasher(current->Value()->first) & (newBucketCount - 1);
                    current->Next = newBuckets[index];
                    newBuckets[index] = current;
                    current = next;
                }
            }

            if (mBuckets)
                mBucketAllocator.Deallocate(mBuckets, mBucketCount);

            mBuckets = newBuckets;
            mBucketCount = newBucketCount;
            return true;
        }

        void GrowIfNeeded()
        {
            if (static_cast<float>(mElementCount) > static_cast<float>(mBucketCount) * kMaxLoadFactor)
                ReallocateBuckets(mBucketCount * 2);
        }

        // Takes a node off the free list when there is one, otherwise allocates.
        template<typename... Args>
        Node* AcquireNode(Args&&... arguments)
        {
            Node* node = nullptr;
            if (mFreeList)
            {
                node = mFreeList;
                mFreeList = mFreeList->Next;
                --mFreeCount;
            }
            else
            {
                node = mNodeAllocator.Allocate(1);
                if (!node) return nullptr;
            }

            mValueAllocator.Construct(node->Value(), std::forward<Args>(arguments)...);
            node->Next = nullptr;
            return node;
        }

        // Destroys the element but keeps the node's storage for the next insert.
        void ReleaseNode(Node* node) noexcept
        {
            mValueAllocator.Destroy(node->Value());
            node->Next = mFreeList;
            mFreeList = node;
            ++mFreeCount;
        }

        void FreeNodeStorage() noexcept
        {
            while (mFreeList)
            {
                Node* next = mFreeList->Next;
                mNodeAllocator.Deallocate(mFreeList, 1);
                mFreeList = next;
            }
            mFreeCount = 0;
        }

        [[nodiscard]] Node* FindNode(const TKey& key) const noexcept
        {
            if (!mBuckets) return nullptr;

            // const on this method does not propagate through Node**, so a const map
            // can still hand a Node* to the non-const Find. Both Find overloads
            // re-apply the right constness to what they return.
            Node* current = mBuckets[BucketIndexFor(key)];
            while (current)
            {
                if (current->Value()->first == key) return current;
                current = current->Next;
            }
            return nullptr;
        }

        // Links an already-built node at the head of its bucket.
        void LinkNode(Node* node, SizeType bucketIndex) noexcept
        {
            node->Next = mBuckets[bucketIndex];
            mBuckets[bucketIndex] = node;
            ++mElementCount;
        }

        // =====================================================================
        // ITERATOR
        // =====================================================================
        // One template for both constnesses — the two hand-written copies the map
        // used to carry drifted apart in the details and doubled every fix.
        template<bool IsConst>
        class IteratorBase
        {
            friend class HashMap;

            using BucketPtr = std::conditional_t<IsConst, Node* const*, Node**>;
            using NodePtr = std::conditional_t<IsConst, const Node*, Node*>;

            BucketPtr mBucketsPtr = nullptr;
            SizeType mBucketCountValue = 0;
            SizeType mCurrentBucket = 0;
            NodePtr mCurrentNode = nullptr;

            void AdvanceToNextOccupied() noexcept
            {
                while (!mCurrentNode && mCurrentBucket + 1 < mBucketCountValue)
                {
                    ++mCurrentBucket;
                    mCurrentNode = mBucketsPtr[mCurrentBucket];
                }
                if (!mCurrentNode) mCurrentBucket = mBucketCountValue;
            }

            IteratorBase(BucketPtr buckets, SizeType bucketCount, SizeType bucket, NodePtr node) noexcept
                : mBucketsPtr(buckets), mBucketCountValue(bucketCount), mCurrentBucket(bucket), mCurrentNode(node)
            {
                if (!mCurrentNode && mCurrentBucket < mBucketCountValue)
                    AdvanceToNextOccupied();
            }

        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = ValueType;
            using difference_type = std::ptrdiff_t;
            using pointer = std::conditional_t<IsConst, const ValueType*, ValueType*>;
            using reference = std::conditional_t<IsConst, const ValueType&, ValueType&>;

            IteratorBase() noexcept = default;

            reference operator*() const noexcept { return *mCurrentNode->Value(); }
            pointer operator->() const noexcept { return mCurrentNode->Value(); }

            IteratorBase& operator++() noexcept
            {
                if (mCurrentNode)
                {
                    mCurrentNode = mCurrentNode->Next;
                    if (!mCurrentNode) AdvanceToNextOccupied();
                }
                return *this;
            }

            IteratorBase operator++(int) noexcept
            {
                IteratorBase temp = *this;
                ++(*this);
                return temp;
            }

            bool operator==(const IteratorBase& other) const noexcept
            {
                return mCurrentNode == other.mCurrentNode && mCurrentBucket == other.mCurrentBucket;
            }

            bool operator!=(const IteratorBase& other) const noexcept { return !(*this == other); }

            // Const iterators are constructible from mutable ones, never the reverse.
            template<bool C = IsConst, typename = std::enable_if_t<!C>>
            operator IteratorBase<true>() const noexcept
            {
                return IteratorBase<true>(mBucketsPtr, mBucketCountValue, mCurrentBucket, mCurrentNode);
            }
        };

    public:
        using Iterator = IteratorBase<false>;
        using ConstIterator = IteratorBase<true>;

        // =====================================================================
        // CONSTRUCTORS & DESTRUCTOR
        // =====================================================================

        // Buckets are allocated on first use, so an empty map costs nothing.
        HashMap() noexcept = default;

        explicit HashMap(SizeType initialBucketCount)
        {
            ReallocateBuckets(NextPowerOfTwo(initialBucketCount));
        }

        ~HashMap() noexcept
        {
            Clear();
            FreeNodeStorage();
            if (mBuckets)
                mBucketAllocator.Deallocate(mBuckets, mBucketCount);
        }

        HashMap(const HashMap& other)
            : mHasher(other.mHasher)
        {
            Reserve(other.mElementCount);
            for (const auto& entry : other)
                Insert(entry.first, entry.second);
        }

        HashMap(HashMap&& other) noexcept
            : mBuckets(other.mBuckets)
            , mBucketCount(other.mBucketCount)
            , mElementCount(other.mElementCount)
            , mFreeList(other.mFreeList)
            , mFreeCount(other.mFreeCount)
            , mHasher(std::move(other.mHasher))
        {
            other.mBuckets = nullptr;
            other.mBucketCount = 0;
            other.mElementCount = 0;
            other.mFreeList = nullptr;
            other.mFreeCount = 0;
        }

        HashMap& operator=(const HashMap& other)
        {
            if (this != &other)
            {
                Clear();
                mHasher = other.mHasher;
                Reserve(other.mElementCount);
                for (const auto& entry : other)
                    Insert(entry.first, entry.second);
            }
            return *this;
        }

        HashMap& operator=(HashMap&& other) noexcept
        {
            if (this != &other)
            {
                Clear();
                FreeNodeStorage();
                if (mBuckets)
                    mBucketAllocator.Deallocate(mBuckets, mBucketCount);

                mBuckets = other.mBuckets;
                mBucketCount = other.mBucketCount;
                mElementCount = other.mElementCount;
                mFreeList = other.mFreeList;
                mFreeCount = other.mFreeCount;
                mHasher = std::move(other.mHasher);

                other.mBuckets = nullptr;
                other.mBucketCount = 0;
                other.mElementCount = 0;
                other.mFreeList = nullptr;
                other.mFreeCount = 0;
            }
            return *this;
        }

        // =====================================================================
        // INSERTION
        // =====================================================================

        // Adds the pair unless the key is already there. false = key present,
        // nothing written (use InsertOrAssign to overwrite).
        bool Insert(const TKey& key, const TValue& value)
        {
            return InsertInternal(key, value);
        }

        bool Insert(TKey&& key, TValue&& value)
        {
            return InsertInternal(std::move(key), std::move(value));
        }

        // Writes the value whether or not the key was there. Returns true when the
        // key was new, false when an existing value was overwritten.
        bool InsertOrAssign(const TKey& key, const TValue& value)
        {
            if (Node* existing = FindNode(key))
            {
                existing->Value()->second = value;
                return false;
            }
            InsertInternal(key, value);
            return true;
        }

        bool InsertOrAssign(const TKey& key, TValue&& value)
        {
            if (Node* existing = FindNode(key))
            {
                existing->Value()->second = std::move(value);
                return false;
            }
            InsertInternal(key, std::move(value));
            return true;
        }

        // Constructs the value in place from `arguments`. Like Insert, it does
        // nothing when the key is already present.
        template<typename... Args>
        bool Emplace(const TKey& key, Args&&... arguments)
        {
            if (!EnsureBuckets()) return false;
            if (FindNode(key)) return false;

            Node* node = AcquireNode(std::piecewise_construct,
                                     std::forward_as_tuple(key),
                                     std::forward_as_tuple(std::forward<Args>(arguments)...));
            if (!node) return false;

            LinkNode(node, BucketIndexFor(key));
            GrowIfNeeded();
            return true;
        }

        // =====================================================================
        // LOOKUP
        // =====================================================================

        TValue* Find(const TKey& key) noexcept
        {
            Node* node = FindNode(key);
            return node ? &node->Value()->second : nullptr;
        }

        const TValue* Find(const TKey& key) const noexcept
        {
            const Node* node = FindNode(key);
            return node ? &node->Value()->second : nullptr;
        }

        [[nodiscard]] bool Contains(const TKey& key) const noexcept { return FindNode(key) != nullptr; }

        // Inserts a default-constructed value when the key is absent. The returned
        // reference stays valid until that key is removed — a rehash relinks nodes
        // but never moves them.
        TValue& operator[](const TKey& key)
        {
            EnsureBuckets();

            if (Node* existing = FindNode(key))
                return existing->Value()->second;

            Node* node = AcquireNode(key, TValue{});
            LinkNode(node, BucketIndexFor(key));
            GrowIfNeeded();
            return node->Value()->second;
        }

        // =====================================================================
        // REMOVAL
        // =====================================================================

        bool Remove(const TKey& key) noexcept
        {
            if (!mBuckets) return false;

            const SizeType index = BucketIndexFor(key);
            Node* current = mBuckets[index];
            Node* previous = nullptr;

            while (current)
            {
                if (current->Value()->first == key)
                {
                    if (previous)
                        previous->Next = current->Next;
                    else
                        mBuckets[index] = current->Next;

                    ReleaseNode(current);
                    --mElementCount;
                    return true;
                }
                previous = current;
                current = current->Next;
            }

            return false;
        }

        // Empties the map but keeps both the bucket array and the node storage, so
        // refilling it costs no allocations. ShrinkToFit() releases them.
        void Clear() noexcept
        {
            for (SizeType i = 0; i < mBucketCount; ++i)
            {
                Node* current = mBuckets[i];
                while (current)
                {
                    Node* next = current->Next;
                    ReleaseNode(current);
                    current = next;
                }
                mBuckets[i] = nullptr;
            }
            mElementCount = 0;
        }

        // Hands the pooled node storage back to the allocator.
        void ShrinkToFit() noexcept { FreeNodeStorage(); }

        // =====================================================================
        // CAPACITY
        // =====================================================================

        [[nodiscard]] SizeType Size() const noexcept { return mElementCount; }
        [[nodiscard]] bool IsEmpty() const noexcept { return mElementCount == 0; }
        [[nodiscard]] SizeType BucketCount() const noexcept { return mBucketCount; }

        // Elements this map holds before the next rehash.
        [[nodiscard]] SizeType Capacity() const noexcept
        {
            return static_cast<SizeType>(static_cast<float>(mBucketCount) * kMaxLoadFactor);
        }

        [[nodiscard]] float LoadFactor() const noexcept
        {
            return mBucketCount > 0 ? static_cast<float>(mElementCount) / static_cast<float>(mBucketCount) : 0.0f;
        }

        [[nodiscard]] static constexpr float MaxLoadFactor() noexcept { return kMaxLoadFactor; }

        // Sizes the map so `count` elements fit without rehashing.
        void Reserve(SizeType count)
        {
            const SizeType required = NextPowerOfTwo(
                static_cast<SizeType>(static_cast<float>(count) / kMaxLoadFactor) + 1);
            if (required > mBucketCount)
                ReallocateBuckets(required);
        }

        // Rounded up to a power of two, and never below what the current elements need.
        void Rehash(SizeType bucketCount)
        {
            SizeType target = NextPowerOfTwo(bucketCount);
            const SizeType minimum = NextPowerOfTwo(
                static_cast<SizeType>(static_cast<float>(mElementCount) / kMaxLoadFactor) + 1);
            if (target < minimum) target = minimum;
            ReallocateBuckets(target);
        }

        void Swap(HashMap& other) noexcept
        {
            std::swap(mBuckets, other.mBuckets);
            std::swap(mBucketCount, other.mBucketCount);
            std::swap(mElementCount, other.mElementCount);
            std::swap(mFreeList, other.mFreeList);
            std::swap(mFreeCount, other.mFreeCount);
            std::swap(mHasher, other.mHasher);
        }

        // =====================================================================
        // ITERATORS
        // =====================================================================

        Iterator Begin() noexcept { return Iterator(mBuckets, mBucketCount, 0, mBucketCount ? mBuckets[0] : nullptr); }
        Iterator End() noexcept { return Iterator(mBuckets, mBucketCount, mBucketCount, nullptr); }

        ConstIterator Begin() const noexcept { return ConstIterator(mBuckets, mBucketCount, 0, mBucketCount ? mBuckets[0] : nullptr); }
        ConstIterator End() const noexcept { return ConstIterator(mBuckets, mBucketCount, mBucketCount, nullptr); }

        Iterator begin() noexcept { return Begin(); }
        Iterator end() noexcept { return End(); }
        ConstIterator begin() const noexcept { return Begin(); }
        ConstIterator end() const noexcept { return End(); }
        ConstIterator cbegin() const noexcept { return Begin(); }
        ConstIterator cend() const noexcept { return End(); }

    private:
        template<typename K, typename V>
        bool InsertInternal(K&& key, V&& value)
        {
            if (!EnsureBuckets()) return false;
            if (FindNode(key)) return false;

            Node* node = AcquireNode(std::forward<K>(key), std::forward<V>(value));
            if (!node) return false;

            // Take the index from the node's own key: `key` may have been moved from.
            LinkNode(node, BucketIndexFor(node->Value()->first));
            GrowIfNeeded();
            return true;
        }
    };

    // Transitional name. HashMap is the spelling that matches HashSet, DynamicArray and
    // the rest of PluSTL; GameHashMap stays for out-of-tree callers until they have moved.
    // Nothing in the engine names it any more.
    template
    <
        typename TKey,
        typename TValue,
        typename THasher = DefaultHash<TKey>,
        typename TAllocator = DefaultAllocator<std::pair<TKey, TValue>>
    >
    using GameHashMap = HashMap<TKey, TValue, THasher, TAllocator>;

} // namespace Plu

#endif //PLUSTL_HASHMAP_H
