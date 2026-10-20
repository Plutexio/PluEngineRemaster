//
// Created by Plutex on 2026-09-18.
//

#include "TestFramework.h"

#include "PluSTL/HashMap/HashMap.h"
#include "PluSTL/String/String.h"

#include <utility>

namespace
{
    // Counts construction and destruction so the tests can prove the free list reuses
    // storage without leaking elements.
    struct Tracked
    {
        static inline int Live = 0;
        static inline int Constructed = 0;

        int Value = 0;

        Tracked() { ++Live; ++Constructed; }
        explicit Tracked(int value) : Value(value) { ++Live; ++Constructed; }
        Tracked(const Tracked& other) : Value(other.Value) { ++Live; ++Constructed; }
        Tracked(Tracked&& other) noexcept : Value(other.Value) { ++Live; ++Constructed; }
        Tracked& operator=(const Tracked&) = default;
        Tracked& operator=(Tracked&&) noexcept = default;
        ~Tracked() { --Live; }

        static void Reset() { Live = 0; Constructed = 0; }
    };

    // Every key lands in the same bucket, so the chain logic is exercised directly.
    struct CollidingHash
    {
        std::size_t operator()(int) const noexcept { return 0; }
    };
}

// ============================================================================
// Basics
// ============================================================================

PLU_TEST(HashMap_StartsEmptyAndAllocatesNothing)
{
    Plu::HashMap<int, int> map;
    PLU_CHECK_EQ(map.Size(), std::size_t{0});
    PLU_CHECK(map.IsEmpty());
    PLU_CHECK_EQ(map.BucketCount(), std::size_t{0});

    // Every read path must tolerate a map that never allocated its buckets.
    PLU_CHECK(map.Find(1) == nullptr);
    PLU_CHECK_FALSE(map.Contains(1));
    PLU_CHECK_FALSE(map.Remove(1));
    PLU_CHECK(map.Begin() == map.End());
}

PLU_TEST(HashMap_InsertFindRemove)
{
    Plu::HashMap<int, int> map;

    PLU_CHECK(map.Insert(1, 10));
    PLU_CHECK(map.Insert(2, 20));
    PLU_CHECK_FALSE(map.Insert(1, 99)); // duplicate key is rejected, value untouched

    PLU_CHECK_EQ(map.Size(), std::size_t{2});
    PLU_CHECK_EQ(*map.Find(1), 10);
    PLU_CHECK_EQ(*map.Find(2), 20);
    PLU_CHECK(map.Find(3) == nullptr);

    PLU_CHECK(map.Remove(1));
    PLU_CHECK_FALSE(map.Remove(1));
    PLU_CHECK_EQ(map.Size(), std::size_t{1});
    PLU_CHECK(map.Find(1) == nullptr);
    PLU_CHECK_EQ(*map.Find(2), 20);
}

PLU_TEST(HashMap_InsertOrAssignOverwrites)
{
    Plu::HashMap<int, int> map;

    PLU_CHECK(map.InsertOrAssign(1, 10));        // true = the key was new
    PLU_CHECK_FALSE(map.InsertOrAssign(1, 11));  // false = an existing value was replaced
    PLU_CHECK_EQ(*map.Find(1), 11);
    PLU_CHECK_EQ(map.Size(), std::size_t{1});
}

PLU_TEST(HashMap_EmplaceConstructsInPlace)
{
    Tracked::Reset();
    {
        Plu::HashMap<int, Tracked> map;
        PLU_CHECK(map.Emplace(1, 42));
        PLU_CHECK_FALSE(map.Emplace(1, 43)); // present already, nothing constructed
        PLU_CHECK_EQ(map.Find(1)->Value, 42);
    }
    PLU_CHECK_EQ(Tracked::Live, 0);
}

PLU_TEST(HashMap_SubscriptDefaultConstructs)
{
    Plu::HashMap<int, int> map;
    PLU_CHECK_EQ(map[7], 0);
    map[7] = 70;
    PLU_CHECK_EQ(*map.Find(7), 70);
    PLU_CHECK_EQ(map.Size(), std::size_t{1});
}

// ============================================================================
// The guarantees the engine relies on
// ============================================================================

PLU_TEST(HashMap_PointersSurviveRehash)
{
    // This is the reason the map stayed on chaining: Find/operator[] hand back a
    // TValue* and callers keep it across further inserts.
    Plu::HashMap<int, int> map;
    map.Insert(0, 100);
    int* held = map.Find(0);
    PLU_CHECK(held != nullptr);

    const std::size_t bucketsBefore = map.BucketCount();
    for (int i = 1; i < 2000; ++i) map.Insert(i, i * 10);
    PLU_CHECK(map.BucketCount() > bucketsBefore); // it really did rehash

    PLU_CHECK_EQ(held, map.Find(0)); // same address
    PLU_CHECK_EQ(*held, 100);
}

PLU_TEST(HashMap_SurvivesUseAfterMove)
{
    // The old map left BucketCount at 0 after a move and then did `hash % 0` on the
    // next operation, i.e. SIGFPE.
    Plu::HashMap<int, int> source;
    source.Insert(1, 10);

    Plu::HashMap<int, int> moved = std::move(source);
    PLU_CHECK_EQ(*moved.Find(1), 10);

    PLU_CHECK(source.Insert(2, 20));
    PLU_CHECK_EQ(*source.Find(2), 20);
    PLU_CHECK_EQ(source[3], 0);
    PLU_CHECK_FALSE(source.Remove(99));
    PLU_CHECK_FALSE(source.Contains(1));
}

PLU_TEST(HashMap_ClearReusesNodeStorage)
{
    // The point of the free list: a map cleared and refilled every frame must stop
    // allocating. Construction still happens; allocation does not.
    Tracked::Reset();
    Plu::HashMap<int, Tracked> map;

    for (int i = 0; i < 64; ++i) map.Emplace(i, i);
    PLU_CHECK_EQ(Tracked::Live, 64);

    map.Clear();
    PLU_CHECK_EQ(map.Size(), std::size_t{0});
    PLU_CHECK_EQ(Tracked::Live, 0); // cleared elements are destroyed, not just unlinked

    for (int i = 0; i < 64; ++i) map.Emplace(i, i * 2);
    PLU_CHECK_EQ(Tracked::Live, 64);
    PLU_CHECK_EQ(map.Find(10)->Value, 20);

    map.ShrinkToFit();
    PLU_CHECK_EQ(map.Size(), std::size_t{64});
    PLU_CHECK_EQ(map.Find(10)->Value, 20);
}

PLU_TEST(HashMap_RemovedNodesAreReused)
{
    Tracked::Reset();
    Plu::HashMap<int, Tracked> map;

    for (int i = 0; i < 32; ++i) map.Emplace(i, i);
    for (int i = 0; i < 32; ++i) PLU_CHECK(map.Remove(i));
    PLU_CHECK_EQ(Tracked::Live, 0);

    for (int i = 100; i < 132; ++i) map.Emplace(i, i);
    PLU_CHECK_EQ(Tracked::Live, 32);
    PLU_CHECK_EQ(map.Size(), std::size_t{32});
    PLU_CHECK_EQ(map.Find(131)->Value, 131);
}

PLU_TEST(HashMap_HandlesFullCollision)
{
    Plu::HashMap<int, int, CollidingHash> map;
    for (int i = 0; i < 100; ++i) PLU_CHECK(map.Insert(i, i * 3));

    PLU_CHECK_EQ(map.Size(), std::size_t{100});
    for (int i = 0; i < 100; ++i) PLU_CHECK_EQ(*map.Find(i), i * 3);

    // Removing from the middle of one long chain must keep the rest linked.
    for (int i = 0; i < 100; i += 2) PLU_CHECK(map.Remove(i));
    PLU_CHECK_EQ(map.Size(), std::size_t{50});
    for (int i = 1; i < 100; i += 2) PLU_CHECK_EQ(*map.Find(i), i * 3);
    for (int i = 0; i < 100; i += 2) PLU_CHECK(map.Find(i) == nullptr);
}

// ============================================================================
// Capacity and growth
// ============================================================================

PLU_TEST(HashMap_BucketCountIsAlwaysAPowerOfTwo)
{
    // BucketIndexFor masks instead of dividing, which is only correct while the
    // bucket count is a power of two. Reserve used to produce Count/0.75 + 1.
    Plu::HashMap<int, int> map;
    map.Insert(0, 0);
    PLU_CHECK_EQ(map.BucketCount() & (map.BucketCount() - 1), std::size_t{0});

    for (std::size_t reserve : {std::size_t{1}, std::size_t{10}, std::size_t{100}, std::size_t{1000}})
    {
        Plu::HashMap<int, int> reserved;
        reserved.Reserve(reserve);
        PLU_CHECK_EQ(reserved.BucketCount() & (reserved.BucketCount() - 1), std::size_t{0});
        PLU_CHECK(reserved.Capacity() >= reserve);
    }

    Plu::HashMap<int, int> grown;
    for (int i = 0; i < 5000; ++i)
    {
        grown.Insert(i, i);
        PLU_CHECK_EQ(grown.BucketCount() & (grown.BucketCount() - 1), std::size_t{0});
    }
    for (int i = 0; i < 5000; ++i) PLU_CHECK_EQ(*grown.Find(i), i);
}

PLU_TEST(HashMap_ReserveAvoidsRehashing)
{
    using IntMap = Plu::HashMap<int, int>; // the comma would split the macro argument
    IntMap map;
    map.Reserve(1000);
    const std::size_t buckets = map.BucketCount();

    for (int i = 0; i < 700; ++i) map.Insert(i, i);
    PLU_CHECK_EQ(map.BucketCount(), buckets);
    PLU_CHECK(map.LoadFactor() <= IntMap::MaxLoadFactor());
}

// ============================================================================
// Value semantics
// ============================================================================

PLU_TEST(HashMap_CopyAndMove)
{
    Plu::HashMap<int, int> original;
    for (int i = 0; i < 50; ++i) original.Insert(i, i * 7);

    Plu::HashMap<int, int> copy = original;
    PLU_CHECK_EQ(copy.Size(), std::size_t{50});
    for (int i = 0; i < 50; ++i) PLU_CHECK_EQ(*copy.Find(i), i * 7);

    copy.Insert(999, 1);
    PLU_CHECK_FALSE(original.Contains(999)); // deep copy, not shared buckets

    Plu::HashMap<int, int> assigned;
    assigned.Insert(1234, 5678);
    assigned = original;
    PLU_CHECK_EQ(assigned.Size(), std::size_t{50});
    PLU_CHECK_FALSE(assigned.Contains(1234));

    Plu::HashMap<int, int> moved = std::move(copy);
    PLU_CHECK_EQ(moved.Size(), std::size_t{51});
}

PLU_TEST(HashMap_DestroysEveryElementItStillHolds)
{
    Tracked::Reset();
    {
        Plu::HashMap<int, Tracked> map;
        for (int i = 0; i < 200; ++i) map.Emplace(i, i);
        PLU_CHECK_EQ(Tracked::Live, 200);
    }
    PLU_CHECK_EQ(Tracked::Live, 0);
}

PLU_TEST(HashMap_SwapExchangesContents)
{
    Plu::HashMap<int, int> a;
    Plu::HashMap<int, int> b;
    a.Insert(1, 1);
    b.Insert(2, 2);
    b.Insert(3, 3);

    a.Swap(b);
    PLU_CHECK_EQ(a.Size(), std::size_t{2});
    PLU_CHECK_EQ(b.Size(), std::size_t{1});
    PLU_CHECK(a.Contains(2));
    PLU_CHECK(b.Contains(1));
}

// ============================================================================
// Iteration
// ============================================================================

PLU_TEST(HashMap_IterationVisitsEveryElementOnce)
{
    Plu::HashMap<int, int> map;
    for (int i = 0; i < 300; ++i) map.Insert(i, i * 2);

    std::vector<bool> seen(300, false);
    std::size_t visited = 0;
    for (const auto& entry : map)
    {
        PLU_CHECK(entry.first >= 0 && entry.first < 300);
        PLU_CHECK_FALSE(seen[static_cast<std::size_t>(entry.first)]);
        seen[static_cast<std::size_t>(entry.first)] = true;
        PLU_CHECK_EQ(entry.second, entry.first * 2);
        ++visited;
    }
    PLU_CHECK_EQ(visited, std::size_t{300});

    // PascalCase spelling, matching DynamicArray / Queue / HashSet.
    std::size_t counted = 0;
    for (auto it = map.Begin(); it != map.End(); ++it) ++counted;
    PLU_CHECK_EQ(counted, std::size_t{300});
}

PLU_TEST(HashMap_ConstIteration)
{
    Plu::HashMap<int, int> map;
    map.Insert(1, 10);
    map.Insert(2, 20);

    const auto& constMap = map;
    int sum = 0;
    for (const auto& entry : constMap) sum += entry.second;
    PLU_CHECK_EQ(sum, 30);
    PLU_CHECK_EQ(*constMap.Find(1), 10);
    PLU_CHECK(constMap.cbegin() != constMap.cend());
}

// ============================================================================
// String keys — the most common key type in the engine
// ============================================================================

PLU_TEST(HashMap_StringKeysHashByContent)
{
    Plu::HashMap<Plu::String, int> map;
    map.Insert(Plu::String("alpha"), 1);
    map.Insert(Plu::String("beta"), 2);

    // A separately built, equal string must find the same entry — it would not if the
    // hash covered the buffer address rather than the characters.
    Plu::String lookup("alpha");
    PLU_CHECK(map.Find(lookup) != nullptr);
    PLU_CHECK_EQ(*map.Find(lookup), 1);

    // Long enough to be heap-allocated rather than SSO.
    Plu::String longKey("a-key-well-past-any-small-string-optimization-buffer-limit");
    map.Insert(longKey, 3);
    PLU_CHECK_EQ(*map.Find(Plu::String("a-key-well-past-any-small-string-optimization-buffer-limit")), 3);
}
