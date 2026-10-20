//
// Created by Plutex on 2026-08-06.
//

#ifndef PLUSTL_CONCURRENTHASHMAP_H
#define PLUSTL_CONCURRENTHASHMAP_H

#include <cstddef>

#include "PluSTL/Hashers/Default.h"
#include "PluSTL/HashMap/HashMap.h"
#include "PluSTL/Concurrent/Detail/StripedHashTable.h"

namespace Plu
{
    // ========================================================================
    // ConcurrentHashMap — HashMap, striped for several threads
    // ========================================================================
    //
    // Same structure as HashMap (heap Nodes in per-bucket chains) and, as far
    // as it can be, the same API: Insert / Emplace / Contains / Remove / Clear /
    // Size / IsEmpty / Reserve / Rehash mean exactly what they mean there. The
    // striping machinery lives in Detail::StripedHashTable and is shared with
    // ConcurrentHashSet; see that header for how the stripes work.
    //
    // Two things cannot carry over from HashMap, both for the same reason —
    // a raw handle into the storage dangles the moment another thread rehashes or
    // removes the node (see Concurrent.h, rule 1):
    //
    //   HashMap::Find -> TValue*        becomes  Find(key, TValue& out) -> bool
    //   HashMap::operator[] -> TValue&  becomes  Visit / VisitOrInsert
    //   begin()/end()                   becomes  ForEach / Snapshot
    //
    // Not copyable / not movable — it is meant to be owned by one subsystem for
    // its whole lifetime.
    template
    <
        typename TKey,
        typename TValue,
        typename THasher = DefaultHash<TKey>
    >
    class ConcurrentHashMap : private Detail::StripedHashTable<TKey,
                                                               Detail::KeyValueEntryTraits<TKey, TValue>,
                                                               THasher>
    {
        using Base = Detail::StripedHashTable<TKey, Detail::KeyValueEntryTraits<TKey, TValue>, THasher>;
        using Entry = typename Base::EntryType;

    public:
        using KeyType = TKey;
        using MappedType = TValue;
        using ValueType = Entry;
        using SizeType = typename Base::SizeType;
        using HasherType = THasher;

        using Base::kStripeCount;
        using Base::kStripeMask;
        using Base::kMinBucketCount;
        using Base::kMaxLoadFactor;

        ConcurrentHashMap() = default;
        explicit ConcurrentHashMap(SizeType initialBucketCount) : Base(initialBucketCount) {}

        // ====================================================================
        // SHARED WITH HashMap — identical names, identical meaning
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

        // ====================================================================
        // WRITES
        // ====================================================================

        // Inserts only when the key is absent. Returns false when it was already there.
        bool Insert(const TKey& key, const TValue& value)
        {
            return this->InsertNode(this->AllocateNode(key, value), KeepExisting{});
        }

        bool Insert(const TKey& key, TValue&& value)
        {
            return this->InsertNode(this->AllocateNode(key, static_cast<TValue&&>(value)), KeepExisting{});
        }

        bool Insert(TKey&& key, TValue&& value)
        {
            return this->InsertNode(this->AllocateNode(static_cast<TKey&&>(key), static_cast<TValue&&>(value)),
                                    KeepExisting{});
        }

        // Constructs the value in place from `args`. Like HashMap::Emplace, it does
        // nothing when the key already exists (returns false).
        template<typename... Args>
        bool Emplace(const TKey& key, Args&&... args)
        {
            return this->InsertNode(this->AllocateNode(Detail::PiecewiseTag{}, key, static_cast<Args&&>(args)...),
                                    KeepExisting{});
        }

        // Inserts, or overwrites the value of an existing key. Returns true when a new
        // entry was created.
        bool InsertOrAssign(const TKey& key, const TValue& value)
        {
            return this->InsertNode(this->AllocateNode(key, value), AssignExisting{});
        }

        bool InsertOrAssign(const TKey& key, TValue&& value)
        {
            return this->InsertNode(this->AllocateNode(key, static_cast<TValue&&>(value)), AssignExisting{});
        }

        // ====================================================================
        // READS — always by value, never by reference
        // ====================================================================

        // Copies the value out. False when the key is absent (outValue untouched).
        // This is HashMap::Find, minus the pointer it cannot safely hand out.
        bool Find(const TKey& key, TValue& outValue) const
        {
            return this->VisitEntry(key, [&outValue](const Entry& entry) { outValue = entry.Value; });
        }

        // The read half of HashMap's operator[]: the stored value, or `fallback` when
        // the key is absent. Never inserts.
        [[nodiscard]] TValue FindOr(const TKey& key, const TValue& fallback) const
        {
            TValue result = fallback;
            this->VisitEntry(key, [&result](const Entry& entry) { result = entry.Value; });
            return result;
        }

        // ====================================================================
        // VISITORS — in-place access without ever exposing a reference
        // ====================================================================

        // Calls fn(TValue&) with the stripe held. False when the key is absent.
        template<typename Fn>
        bool Visit(const TKey& key, Fn&& fn)
        {
            return this->VisitEntry(key, [&fn](Entry& entry) { fn(entry.Value); });
        }

        // Inserts defaultValue when the key is absent, then calls fn(TValue&) on the entry —
        // existing or freshly created — with the stripe held. This is the accumulate
        // primitive: "bump the counter for this key, creating it if it is the first sample".
        // The write half of HashMap's operator[].
        template<typename Fn>
        void VisitOrInsert(const TKey& key, Fn&& fn, const TValue& defaultValue)
        {
            // Allocated up front so no malloc happens inside the spinlock; freed again by
            // the base when the key turned out to exist.
            this->VisitOrInsertNode(this->AllocateNode(key, defaultValue),
                                    [&fn](Entry& entry) { fn(entry.Value); });
        }

        // Calls fn(const TKey&, const TValue&) for every entry, one stripe at a time. The map
        // is NOT frozen for the whole walk — only the stripe being visited is locked, so an
        // entry inserted into an already-visited stripe will be missed. Use Snapshot() when a
        // consistent view matters more than lock hold time.
        template<typename Fn>
        void ForEach(Fn&& fn) const
        {
            this->ForEachEntry([&fn](const Entry& entry) { fn(entry.Key, entry.Value); });
        }

        // A plain, unsynchronized HashMap for readers that want a frozen view (a UI
        // panel, a CSV dump) or the iterators this type cannot have. Built stripe by stripe,
        // so it is a "recent" rather than an instantaneous view.
        [[nodiscard]] HashMap<TKey, TValue, THasher> Snapshot() const
        {
            HashMap<TKey, TValue, THasher> copy;
            copy.Reserve(this->Size());
            ForEach([&copy](const TKey& key, const TValue& value) { copy.Insert(key, value); });
            return copy;
        }

        // Empties the map and hands over its contents as one batch, atomically with respect
        // to every other map operation — the ConcurrentHashSet::DrainToArray of maps.
        // fn(const TKey&, TValue&&).
        template<typename Fn>
        void Drain(Fn&& fn)
        {
            this->DrainEntries([&fn](Entry&& entry)
            {
                fn(static_cast<const TKey&>(entry.Key), static_cast<TValue&&>(entry.Value));
            });
        }

    private:
        // The two ways InsertNode may resolve a key that is already present.
        struct KeepExisting
        {
            void operator()(Entry&, Entry&) const noexcept {}
        };

        struct AssignExisting
        {
            void operator()(Entry& existing, Entry& fresh) const
            {
                existing.Value = static_cast<TValue&&>(fresh.Value);
            }
        };
    };
}

#endif //PLUSTL_CONCURRENTHASHMAP_H
