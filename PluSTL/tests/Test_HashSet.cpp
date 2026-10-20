//
// Created by Plutex on 2026-09-18.
//

#include "TestFramework.h"

#include "PluSTL/HashSet/HashSet.h"
#include "PluSTL/String/String.h"

#include <utility>
#include <vector>

namespace
{
    struct TrackedValue
    {
        static inline int Live = 0;
        int Value = 0;

        TrackedValue() { ++Live; }
        explicit TrackedValue(int value) : Value(value) { ++Live; }
        TrackedValue(const TrackedValue& other) : Value(other.Value) { ++Live; }
        TrackedValue(TrackedValue&& other) noexcept : Value(other.Value) { ++Live; }
        TrackedValue& operator=(const TrackedValue&) = default;
        TrackedValue& operator=(TrackedValue&&) noexcept = default;
        ~TrackedValue() { --Live; }

        bool operator==(const TrackedValue& other) const { return Value == other.Value; }
        static void Reset() { Live = 0; }
    };

    struct CollidingHash
    {
        std::size_t operator()(const TrackedValue&) const noexcept { return 0; }
        std::size_t operator()(int) const noexcept { return 0; }
    };

    // TrackedValue has a user-provided destructor, so it is not trivially copyable and
    // DefaultHash's byte-wise fallback refuses to compile for it. That is the intended
    // behaviour — a key type like this states how it hashes.
    struct TrackedHash
    {
        std::size_t operator()(const TrackedValue& value) const noexcept
        {
            return Plu::DefaultHash<int>{}(value.Value);
        }
    };
}

PLU_TEST(HashSet_InsertContainsRemove)
{
    Plu::HashSet<int> set;
    PLU_CHECK(set.IsEmpty());

    PLU_CHECK(set.Insert(1));
    PLU_CHECK(set.Insert(2));
    PLU_CHECK_FALSE(set.Insert(1));
    PLU_CHECK_EQ(set.Size(), std::size_t{2});

    PLU_CHECK(set.Contains(1));
    PLU_CHECK_FALSE(set.Contains(3));

    PLU_CHECK(set.Remove(1));
    PLU_CHECK_FALSE(set.Remove(1));
    PLU_CHECK_FALSE(set.Contains(1));
    PLU_CHECK(set.Contains(2));
}

PLU_TEST(HashSet_EmplaceBuildsTheElement)
{
    TrackedValue::Reset();
    {
        Plu::HashSet<TrackedValue, TrackedHash> set;
        PLU_CHECK(set.Emplace(7));
        PLU_CHECK_FALSE(set.Emplace(7));
        PLU_CHECK(set.Contains(TrackedValue{7}));
        PLU_CHECK_EQ(set.Size(), std::size_t{1});
    }
    PLU_CHECK_EQ(TrackedValue::Live, 0);
}

PLU_TEST(HashSet_PascalCaseIteration)
{
    Plu::HashSet<int> set;
    for (int i = 0; i < 100; ++i) set.Insert(i);

    std::size_t viaBegin = 0;
    for (auto it = set.Begin(); it != set.End(); ++it) ++viaBegin;
    PLU_CHECK_EQ(viaBegin, std::size_t{100});

    std::size_t viaRangeFor = 0;
    for (int value : set) { (void)value; ++viaRangeFor; }
    PLU_CHECK_EQ(viaRangeFor, std::size_t{100});

    const auto& constSet = set;
    std::size_t viaConst = 0;
    for (auto it = constSet.Begin(); it != constSet.End(); ++it) ++viaConst;
    PLU_CHECK_EQ(viaConst, std::size_t{100});
}

PLU_TEST(HashSet_SurvivesTombstoneDrivenRehash)
{
    // Remove() triggers Rehash once deleted slots pass half the capacity. That
    // migration used to go through the growth check and could re-enter Rehash while
    // still reading the old table.
    Plu::HashSet<int> set;
    for (int i = 0; i < 1000; ++i) set.Insert(i);

    for (int round = 0; round < 10; ++round)
    {
        for (int i = 0; i < 1000; ++i) PLU_CHECK(set.Remove(i));
        PLU_CHECK(set.IsEmpty());
        for (int i = 0; i < 1000; ++i) PLU_CHECK(set.Insert(i));
        PLU_CHECK_EQ(set.Size(), std::size_t{1000});
    }

    for (int i = 0; i < 1000; ++i) PLU_CHECK(set.Contains(i));
}

PLU_TEST(HashSet_RehashAtTheLoadFactorBoundary)
{
    // Rehash(mCapacity) with the table close to its maximum load is where the
    // re-entrancy window was.
    Plu::HashSet<int> set(16);
    const std::size_t capacity = set.Capacity();
    const auto target = static_cast<int>(static_cast<float>(capacity) * 0.74f);

    for (int i = 0; i < target; ++i) set.Insert(i);
    for (int i = 0; i < target / 2; ++i) set.Remove(i);
    set.Rehash(set.Capacity());

    for (int i = target / 2; i < target; ++i) PLU_CHECK(set.Contains(i));
    PLU_CHECK_EQ(set.Size(), static_cast<std::size_t>(target - target / 2));
}

PLU_TEST(HashSet_HandlesFullCollision)
{
    Plu::HashSet<int, CollidingHash> set;
    for (int i = 0; i < 200; ++i) PLU_CHECK(set.Insert(i));
    PLU_CHECK_EQ(set.Size(), std::size_t{200});
    for (int i = 0; i < 200; ++i) PLU_CHECK(set.Contains(i));

    for (int i = 0; i < 200; i += 3) PLU_CHECK(set.Remove(i));
    for (int i = 0; i < 200; ++i) PLU_CHECK_EQ(set.Contains(i), (i % 3) != 0);
}

PLU_TEST(HashSet_SurvivesUseAfterMove)
{
    Plu::HashSet<int> source;
    source.Insert(1);

    Plu::HashSet<int> moved = std::move(source);
    PLU_CHECK(moved.Contains(1));

    PLU_CHECK(source.Insert(2));
    PLU_CHECK(source.Contains(2));
    PLU_CHECK_FALSE(source.Remove(99));
}

PLU_TEST(HashSet_CopyAndMove)
{
    Plu::HashSet<int> original;
    for (int i = 0; i < 50; ++i) original.Insert(i);

    Plu::HashSet<int> copy = original;
    PLU_CHECK_EQ(copy.Size(), std::size_t{50});
    copy.Insert(999);
    PLU_CHECK_FALSE(original.Contains(999));

    Plu::HashSet<int> assigned;
    assigned.Insert(1234);
    assigned = original;
    PLU_CHECK_FALSE(assigned.Contains(1234));
    PLU_CHECK_EQ(assigned.Size(), std::size_t{50});
}

PLU_TEST(HashSet_DestroysEveryElementItStillHolds)
{
    TrackedValue::Reset();
    {
        Plu::HashSet<TrackedValue, TrackedHash> set;
        for (int i = 0; i < 200; ++i) set.Emplace(i);
        PLU_CHECK_EQ(TrackedValue::Live, 200);
        set.Clear();
        PLU_CHECK_EQ(TrackedValue::Live, 0);
        for (int i = 0; i < 50; ++i) set.Emplace(i);
    }
    PLU_CHECK_EQ(TrackedValue::Live, 0);
}

PLU_TEST(HashSet_StringElements)
{
    Plu::HashSet<Plu::String> set;
    set.Insert(Plu::String("alpha"));
    set.Insert(Plu::String("beta"));

    PLU_CHECK(set.Contains(Plu::String("alpha")));
    PLU_CHECK_FALSE(set.Contains(Plu::String("gamma")));
    PLU_CHECK_FALSE(set.Insert(Plu::String("alpha")));
    PLU_CHECK_EQ(set.Size(), std::size_t{2});
}
