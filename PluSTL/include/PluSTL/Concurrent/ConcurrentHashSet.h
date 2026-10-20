//
// Created by Plutex on 2026-08-06.
//

#ifndef PLUSTL_CONCURRENTHASHSET_H
#define PLUSTL_CONCURRENTHASHSET_H

#include <cstddef>

#include "PluSTL/Hashers/Default.h"
#include "PluSTL/Array/Array.h"
#include "PluSTL/HashSet/HashSet.h"
#include "PluSTL/Concurrent/Detail/StripedHashTable.h"

namespace Plu
{
    // ========================================================================
    // ConcurrentHashSet — HashSet, striped for several threads
    // ========================================================================
    //
    // The API HashSet has, minus what cannot survive concurrency: Insert /
    // Contains / Remove / Clear / Size / IsEmpty / Reserve / Rehash /
    // LoadFactor mean exactly what they mean there. Storage is separate chaining
    // rather than HashSet's open addressing (open addressing moves elements on
    // growth, which fights striping); the striping machinery lives in
    // Detail::StripedHashTable and is shared with ConcurrentHashMap.
    //
    // What cannot carry over (see Concurrent.h, rule 1 — no handle into the
    // storage ever escapes):
    //
    //   HashSet::Find -> Iterator   becomes  Contains
    //   begin()/end()               becomes  ForEach / Snapshot / DrainToArray
    //
    // Insert() returning false for an element that is already there *is* the
    // dedupe primitive — "post this work item unless it is already queued" is one
    // atomic call, no scan under a lock.
    //
    // Not copyable / not movable — it is meant to be owned by one subsystem for
    // its whole lifetime.
    template
    <
        typename T,
        typename THasher = DefaultHash<T>
    >
    class ConcurrentHashSet : private Detail::StripedHashTable<T, Detail::IdentityEntryTraits<T>, THasher>
    {
        using Base = Detail::StripedHashTable<T, Detail::IdentityEntryTraits<T>, THasher>;

    public:
        using ValueType = T;
        using SizeType = typename Base::SizeType;
        using HasherType = THasher;

        using Base::kStripeCount;
        using Base::kStripeMask;
        using Base::kMinBucketCount;
        using Base::kMaxLoadFactor;

        ConcurrentHashSet() = default;
        explicit ConcurrentHashSet(SizeType initialBucketCount) : Base(initialBucketCount) {}

        // ====================================================================
        // SHARED WITH HashSet — identical names, identical meaning
        // ====================================================================
        using Base::Contains;
        using Base::Remove;
        using Base::Clear;
        using Base::Size;
        using Base::IsEmpty;
        using Base::Reserve;
        using Base::Rehash;
        using Base::BucketCount;
        using Base::LoadFactor;

        // Adds the element unless it is already present. False = it was already there.
        bool Insert(const T& value) { return this->InsertNode(this->AllocateNode(value), KeepExisting{}); }
        bool Insert(T&& value) { return this->InsertNode(this->AllocateNode(static_cast<T&&>(value)), KeepExisting{}); }

        // Constructs the element in place from `args`. Same contract as Insert.
        template<typename... Args>
        bool Emplace(Args&&... args)
        {
            return this->InsertNode(this->AllocateNode(static_cast<Args&&>(args)...), KeepExisting{});
        }

        // fn(const T&) per element, one stripe at a time. The set is NOT frozen for the
        // whole walk — only the stripe being visited is locked, so an element inserted into
        // an already-visited stripe will be missed. Use Snapshot() for a frozen view.
        template<typename Fn>
        void ForEach(Fn&& fn) const
        {
            this->ForEachEntry([&fn](const T& value) { fn(value); });
        }

        // A plain, unsynchronized HashSet for readers that want a frozen view or the
        // iterators this type cannot have. Built stripe by stripe, so it is a "recent"
        // rather than an instantaneous view.
        [[nodiscard]] HashSet<T, THasher> Snapshot() const
        {
            HashSet<T, THasher> copy;
            copy.Reserve(this->Size());
            ForEach([&copy](const T& value) { copy.Insert(value); });
            return copy;
        }

        // Empties the set and returns everything it held, as one batch, atomically with
        // respect to other set operations. This is the accumulate-then-consume pattern:
        // many threads Insert() deduped work items, one thread drains and processes them.
        // Nothing can be lost or double-queued between the drain and the processing,
        // because the drain and the reset are the same critical section.
        [[nodiscard]] DynamicArray<T> DrainToArray()
        {
            DynamicArray<T> out;
            out.Reserve(this->Size());
            this->DrainEntries([&out](T&& value) { out.PushBack(static_cast<T&&>(value)); });
            return out;
        }

        // Same batch consume without materializing the array — fn(T&&) per element, called
        // while every stripe is held. The callback rules apply in full (see Concurrent.h).
        template<typename Fn>
        void Drain(Fn&& fn)
        {
            this->DrainEntries([&fn](T&& value) { fn(static_cast<T&&>(value)); });
        }

    private:
        struct KeepExisting
        {
            void operator()(T&, T&) const noexcept {}
        };
    };
}

#endif //PLUSTL_CONCURRENTHASHSET_H
