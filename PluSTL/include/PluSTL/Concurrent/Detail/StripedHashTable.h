//
// Created by Plutex on 2026-08-07.
//

#ifndef PLUSTL_STRIPEDHASHTABLE_H
#define PLUSTL_STRIPEDHASHTABLE_H

#include <atomic>
#include <cstddef>
#include <mutex>

#include "PluSTL/Allocators/Default.h"
#include "PluSTL/Hashers/Default.h"
// Required, not optional: without the String/Path specializations DefaultHash falls back to
// the generic byte-wise FNV, which for a String hashes the object's raw bytes — including the
// uninitialized tail of the SSO buffer. Two equal Strings would then hash differently and
// lookups would miss at random.
#include "PluSTL/Hashers/String.h"
#include "PluSTL/Concurrent/LockPrimitives.h"

namespace Plu
{
    namespace Detail
    {
        // Tag for "construct the value in place from these arguments" — how Emplace reaches
        // KeyValueEntry's value without materializing a TValue first.
        struct PiecewiseTag {};

        // What a ConcurrentHashMap node stores. A struct with named members rather than a
        // pair, so the visitor callbacks read `entry.Value`, not `entry.second`.
        template<typename TKey, typename TValue>
        struct KeyValueEntry
        {
            TKey Key;
            TValue Value;

            template<typename UKey, typename UValue>
            KeyValueEntry(UKey&& key, UValue&& value)
                : Key(static_cast<UKey&&>(key))
                , Value(static_cast<UValue&&>(value)) {}

            template<typename UKey, typename... Args>
            KeyValueEntry(PiecewiseTag, UKey&& key, Args&&... args)
                : Key(static_cast<UKey&&>(key))
                , Value(static_cast<Args&&>(args)...) {}
        };

        // Entry policies: how the table gets a key out of whatever a node stores.
        template<typename TKey, typename TValue>
        struct KeyValueEntryTraits
        {
            using EntryType = KeyValueEntry<TKey, TValue>;
            static const TKey& KeyOf(const EntryType& entry) noexcept { return entry.Key; }
        };

        // A set stores the key itself.
        template<typename T>
        struct IdentityEntryTraits
        {
            using EntryType = T;
            static const T& KeyOf(const T& entry) noexcept { return entry; }
        };

        // ====================================================================
        // StripedHashTable — the shared core of ConcurrentHashMap / ConcurrentHashSet
        // ====================================================================
        //
        // Separate chaining (heap Nodes in per-bucket chains, same shape as
        // HashMap) with kStripeCount cache-line-padded spinlocks over the
        // buckets. Bucket i is guarded by stripe (i & kStripeMask), so unrelated
        // keys almost never contend.
        //
        // The bucket count is a power of two and never smaller than kStripeCount,
        // which means the low bits of a key's hash pick the stripe regardless of
        // the current bucket count — that is what lets a caller lock a stripe
        // *before* reading the bucket array. It is also why a rehash never moves a
        // node to a different stripe.
        //
        // Chaining rather than HashSet's open addressing: open addressing moves
        // elements around on growth, which fights striping (an element could
        // migrate to a bucket guarded by a different stripe mid-probe).
        //
        // Everything public here is deliberately named exactly as on HashMap /
        // HashSet — Size, IsEmpty, Contains, Remove, Clear, Reserve, Rehash — and
        // is inherited unchanged by both concurrent containers. The parts that
        // cannot be shared (Insert's return contract, the visitors, Snapshot) are
        // protected hooks the derived class wraps.
        template<typename TKey, typename TTraits, typename THasher>
        class StripedHashTable
        {
        public:
            using KeyType = TKey;
            using EntryType = typename TTraits::EntryType;
            using SizeType = std::size_t;
            using HasherType = THasher;

            static constexpr SizeType kStripeCount = 64;
            static constexpr SizeType kStripeMask = kStripeCount - 1;
            static constexpr SizeType kMinBucketCount = kStripeCount;
            static constexpr float kMaxLoadFactor = 0.75f;

            // ================================================================
            // OBSERVERS
            // ================================================================
            // Every count here is a relaxed load: true when it was read, possibly stale by
            // the time the caller looks at it. Same for HashMap::Size() the moment a
            // second thread touches the map — the difference is that this one at least
            // cannot tear.
            [[nodiscard]] SizeType Size() const noexcept { return mSize.load(std::memory_order_relaxed); }
            [[nodiscard]] bool IsEmpty() const noexcept { return Size() == 0; }
            [[nodiscard]] SizeType BucketCount() const noexcept { return mBucketCount.load(std::memory_order_relaxed); }

            [[nodiscard]] float LoadFactor() const noexcept
            {
                const SizeType buckets = BucketCount();
                return buckets > 0 ? static_cast<float>(Size()) / static_cast<float>(buckets) : 0.0f;
            }

            bool Contains(const TKey& key) const
            {
                const SizeType hash = HashOf(key);
                ScopedSpinLock guard(mStripes.Of(hash));
                return FindNodeLocked(hash, key) != nullptr;
            }

            // ================================================================
            // WRITES
            // ================================================================

            // False when the key was not present.
            bool Remove(const TKey& key)
            {
                const SizeType hash = HashOf(key);
                Node* removed = nullptr;

                {
                    ScopedSpinLock guard(mStripes.Of(hash));

                    Node** link = &mBuckets[BucketOf(hash)];
                    while (*link)
                    {
                        if (TTraits::KeyOf((*link)->Entry) == key)
                        {
                            removed = *link;
                            *link = removed->Next;
                            mSize.fetch_sub(1, std::memory_order_relaxed);
                            break;
                        }
                        link = &(*link)->Next;
                    }
                }

                // Destruction can run arbitrary user code (TOwningPointer release, String
                // free) — keep it out of the spinlock.
                if (!removed) return false;
                FreeNode(removed);
                return true;
            }

            // Drops every entry. Takes all stripes, so no other operation on this container
            // can be in flight — but a caller who copied a value out earlier keeps its copy.
            void Clear()
            {
                typename StripeArrayType::ScopedAll guard(mStripes);
                ClearAllStripesHeld();
            }

            // Grows the table so `count` entries fit under the load factor. Never shrinks.
            void Reserve(SizeType count)
            {
                const SizeType required = static_cast<SizeType>(static_cast<float>(count) / kMaxLoadFactor) + 1;
                if (required <= BucketCount()) return;
                Rehash(required);
            }

            // Rebuilds the table with at least `bucketCount` buckets (rounded up to a power
            // of two, never below kMinBucketCount nor below what the current size needs).
            void Rehash(SizeType bucketCount)
            {
                std::lock_guard<std::mutex> growGuard(mGrowMutex);

                SizeType target = RoundUpToPowerOfTwo(bucketCount < kMinBucketCount ? kMinBucketCount : bucketCount);
                while (static_cast<float>(mSize.load(std::memory_order_relaxed)) >
                       static_cast<float>(target) * kMaxLoadFactor)
                {
                    target *= 2;
                }

                if (target == mBucketCount.load(std::memory_order_relaxed)) return;
                RehashUnderGrowMutex(target);
            }

        protected:
            struct Node
            {
                EntryType Entry;
                Node* Next;

                template<typename... Args>
                explicit Node(Args&&... args)
                    : Entry(static_cast<Args&&>(args)...)
                    , Next(nullptr) {}
            };

            using StripeArrayType = StripeArray<kStripeCount>;
            using NodeAllocator = DefaultAllocator<Node>;

            explicit StripedHashTable(SizeType initialBucketCount = kMinBucketCount)
            {
                AllocateBuckets(RoundUpToPowerOfTwo(
                    initialBucketCount < kMinBucketCount ? kMinBucketCount : initialBucketCount));
            }

            ~StripedHashTable()
            {
                // No lock: the destructor of a container nobody else may still be using.
                ClearAllStripesHeld();
                if (mBuckets)
                    ::operator delete(mBuckets);
            }

            // Owned by exactly one subsystem for its whole lifetime — see Concurrent.h.
            StripedHashTable(const StripedHashTable&) = delete;
            StripedHashTable& operator=(const StripedHashTable&) = delete;
            StripedHashTable(StripedHashTable&&) = delete;
            StripedHashTable& operator=(StripedHashTable&&) = delete;

            [[nodiscard]] SizeType HashOf(const TKey& key) const { return mHasher(key); }

            template<typename... Args>
            Node* AllocateNode(Args&&... args)
            {
                Node* node = mNodeAlloc.Allocate(1);
                mNodeAlloc.Construct(node, static_cast<Args&&>(args)...);
                return node;
            }

            void FreeNode(Node* node) noexcept
            {
                mNodeAlloc.Destroy(node);
                mNodeAlloc.Deallocate(node, 1);
            }

            // Takes ownership of `fresh`: links it in when the key is absent, otherwise
            // hands the existing entry to `onExisting` and frees `fresh`. Returns true when
            // a new entry was created — the Insert contract of both containers.
            //
            // `fresh` must already hold the key (that is where the key comes from), so the
            // node is allocated by the caller *outside* the spinlock. That is the point:
            // no malloc ever happens inside a critical section.
            template<typename OnExisting>
            bool InsertNode(Node* fresh, OnExisting&& onExisting)
            {
                const TKey& key = TTraits::KeyOf(fresh->Entry);
                const SizeType hash = HashOf(key);
                bool inserted = false;

                {
                    ScopedSpinLock guard(mStripes.Of(hash));

                    if (Node* found = FindNodeLocked(hash, key))
                    {
                        onExisting(found->Entry, fresh->Entry);
                    }
                    else
                    {
                        LinkNodeLocked(hash, fresh);
                        inserted = true;
                    }
                }

                if (!inserted) FreeNode(fresh);
                else GrowIfNeeded();
                return inserted;
            }

            // fn(EntryType&) with the stripe held. False when the key is absent.
            template<typename Fn>
            bool VisitEntry(const TKey& key, Fn&& fn)
            {
                const SizeType hash = HashOf(key);
                ScopedSpinLock guard(mStripes.Of(hash));

                Node* found = FindNodeLocked(hash, key);
                if (!found) return false;
                fn(found->Entry);
                return true;
            }

            // fn(const EntryType&) with the stripe held — the read-only half, so a const
            // container still has a way to look one key up (Find copies out through it).
            template<typename Fn>
            bool VisitEntry(const TKey& key, Fn&& fn) const
            {
                const SizeType hash = HashOf(key);
                ScopedSpinLock guard(mStripes.Of(hash));

                const Node* found = FindNodeLocked(hash, key);
                if (!found) return false;
                fn(static_cast<const EntryType&>(found->Entry));
                return true;
            }

            // Links `fresh` when the key is absent, then calls fn(EntryType&) on the entry —
            // existing or freshly created — with the stripe held. Returns true when `fresh`
            // was the one linked in; otherwise it is freed here.
            template<typename Fn>
            bool VisitOrInsertNode(Node* fresh, Fn&& fn)
            {
                const TKey& key = TTraits::KeyOf(fresh->Entry);
                const SizeType hash = HashOf(key);
                bool inserted = false;

                {
                    ScopedSpinLock guard(mStripes.Of(hash));

                    Node* found = FindNodeLocked(hash, key);
                    if (!found)
                    {
                        LinkNodeLocked(hash, fresh);
                        found = fresh;
                        inserted = true;
                    }

                    fn(found->Entry);
                }

                if (!inserted) FreeNode(fresh);
                else GrowIfNeeded();
                return inserted;
            }

            // fn(const EntryType&) over everything, one stripe at a time. The container is
            // NOT frozen for the whole walk — only the stripe being visited is locked, so an
            // entry inserted into an already-visited stripe will be missed.
            template<typename Fn>
            void ForEachEntry(Fn&& fn) const
            {
                for (SizeType stripe = 0; stripe < kStripeCount; ++stripe)
                {
                    ScopedSpinLock guard(mStripes.At(stripe));

                    const SizeType buckets = mBucketCount.load(std::memory_order_relaxed);
                    for (SizeType index = stripe; index < buckets; index += kStripeCount)
                    {
                        for (Node* current = mBuckets[index]; current; current = current->Next)
                            fn(static_cast<const EntryType&>(current->Entry));
                    }
                }
            }

            // Empties the container and hands every entry to fn(EntryType&&) — all of it in
            // ONE critical section (every stripe held), so nothing can be lost or delivered
            // twice between the drain and the processing.
            template<typename Fn>
            void DrainEntries(Fn&& fn)
            {
                typename StripeArrayType::ScopedAll guard(mStripes);

                const SizeType buckets = mBucketCount.load(std::memory_order_relaxed);
                for (SizeType i = 0; i < buckets; ++i)
                {
                    Node* current = mBuckets[i];
                    while (current)
                    {
                        Node* next = current->Next;
                        fn(static_cast<EntryType&&>(current->Entry));
                        FreeNode(current);
                        current = next;
                    }
                    mBuckets[i] = nullptr;
                }
                mSize.store(0, std::memory_order_relaxed);
            }

        private:
            // Guarded by the stripes: every stripe must be held to touch mBuckets, one
            // stripe is enough to touch the buckets that map to it. mBucketCount is atomic
            // only so the (approximate) load-factor check may read it outside any lock.
            Node** mBuckets = nullptr;
            std::atomic<SizeType> mBucketCount{0};
            std::atomic<SizeType> mSize{0};

            StripeArrayType mStripes;

            // Serializes rehashes, so a burst of inserts crossing the load factor grows the
            // table once instead of kStripeCount times.
            std::mutex mGrowMutex;

            [[no_unique_address]] HasherType mHasher;
            [[no_unique_address]] NodeAllocator mNodeAlloc;

            static SizeType RoundUpToPowerOfTwo(SizeType value) noexcept
            {
                SizeType result = 1;
                while (result < value) result *= 2;
                return result;
            }

            // Caller must hold the stripe of `hash`.
            SizeType BucketOf(SizeType hash) const noexcept
            {
                return hash & (mBucketCount.load(std::memory_order_relaxed) - 1);
            }

            // Caller must hold the stripe of `hash`.
            Node* FindNodeLocked(SizeType hash, const TKey& key) const
            {
                for (Node* current = mBuckets[BucketOf(hash)]; current; current = current->Next)
                {
                    if (TTraits::KeyOf(current->Entry) == key) return current;
                }
                return nullptr;
            }

            // Caller must hold the stripe of `hash`, and the key must be absent.
            void LinkNodeLocked(SizeType hash, Node* node) noexcept
            {
                const SizeType index = BucketOf(hash);
                node->Next = mBuckets[index];
                mBuckets[index] = node;
                mSize.fetch_add(1, std::memory_order_relaxed);
            }

            void AllocateBuckets(SizeType bucketCount)
            {
                mBuckets = static_cast<Node**>(::operator new(bucketCount * sizeof(Node*)));
                for (SizeType i = 0; i < bucketCount; ++i)
                    mBuckets[i] = nullptr;
                mBucketCount.store(bucketCount, std::memory_order_relaxed);
            }

            // Caller must hold every stripe (or be the destructor).
            void ClearAllStripesHeld() noexcept
            {
                const SizeType buckets = mBucketCount.load(std::memory_order_relaxed);
                for (SizeType i = 0; i < buckets; ++i)
                {
                    Node* current = mBuckets[i];
                    while (current)
                    {
                        Node* next = current->Next;
                        FreeNode(current);
                        current = next;
                    }
                    mBuckets[i] = nullptr;
                }
                mSize.store(0, std::memory_order_relaxed);
            }

            // Grows the table when the load factor is exceeded. Must be called with NO
            // stripe held — it takes all of them, in index order, to keep the lock order
            // global.
            void GrowIfNeeded()
            {
                const SizeType buckets = mBucketCount.load(std::memory_order_relaxed);
                const SizeType size = mSize.load(std::memory_order_relaxed);
                if (static_cast<float>(size) <= static_cast<float>(buckets) * kMaxLoadFactor)
                    return;

                std::lock_guard<std::mutex> growGuard(mGrowMutex);

                // Another thread may have grown the table while we waited on mGrowMutex.
                if (mBucketCount.load(std::memory_order_relaxed) != buckets)
                    return;

                RehashUnderGrowMutex(buckets * 2);
            }

            // Caller must hold mGrowMutex and no stripe. `newBucketCount` must be a power of
            // two >= kMinBucketCount.
            void RehashUnderGrowMutex(SizeType newBucketCount)
            {
                typename StripeArrayType::ScopedAll guard(mStripes);

                const SizeType currentBuckets = mBucketCount.load(std::memory_order_relaxed);
                Node** newBuckets = static_cast<Node**>(::operator new(newBucketCount * sizeof(Node*)));
                for (SizeType i = 0; i < newBucketCount; ++i)
                    newBuckets[i] = nullptr;

                // A node's stripe is the low bits of its hash and the bucket count is a power
                // of two, so relinking never moves a node to a different stripe.
                for (SizeType i = 0; i < currentBuckets; ++i)
                {
                    Node* current = mBuckets[i];
                    while (current)
                    {
                        Node* next = current->Next;
                        const SizeType newIndex = mHasher(TTraits::KeyOf(current->Entry)) & (newBucketCount - 1);
                        current->Next = newBuckets[newIndex];
                        newBuckets[newIndex] = current;
                        current = next;
                    }
                }

                ::operator delete(mBuckets);
                mBuckets = newBuckets;
                mBucketCount.store(newBucketCount, std::memory_order_relaxed);
            }
        };
    }
}

#endif //PLUSTL_STRIPEDHASHTABLE_H
