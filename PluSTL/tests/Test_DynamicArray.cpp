//
// Created by Plutex on 2026-09-18.
//

#include "TestFramework.h"

#include "PluSTL/Array/Array.h"
#include "PluSTL/String/String.h"

#include <utility>

namespace
{
    struct Tracked
    {
        static inline int Live = 0;
        int Value = 0;

        Tracked() { ++Live; }
        explicit Tracked(int value) : Value(value) { ++Live; }
        Tracked(const Tracked& other) : Value(other.Value) { ++Live; }
        Tracked(Tracked&& other) noexcept : Value(other.Value) { ++Live; }
        Tracked& operator=(const Tracked&) = default;
        Tracked& operator=(Tracked&&) noexcept = default;
        ~Tracked() { --Live; }

        bool operator==(const Tracked& other) const { return Value == other.Value; }
        static void Reset() { Live = 0; }
    };
}

// ============================================================================
// Basics
// ============================================================================

PLU_TEST(DynamicArray_PushPopAndAccess)
{
    Plu::DynamicArray<int> array;
    PLU_CHECK(array.IsEmpty());

    for (int i = 0; i < 10; ++i) array.PushBack(i);
    PLU_CHECK_EQ(array.Size(), std::size_t{10});
    PLU_CHECK_EQ(array.Front(), 0);
    PLU_CHECK_EQ(array.Back(), 9);
    PLU_CHECK_EQ(array[5], 5);
    PLU_CHECK_EQ(array.At(5), 5);

    array.PopBack();
    PLU_CHECK_EQ(array.Size(), std::size_t{9});
    PLU_CHECK_EQ(array.Back(), 8);
}

PLU_TEST(DynamicArray_EmplaceBackReturnsTheElement)
{
    Plu::DynamicArray<Tracked> array;
    Tracked& added = array.EmplaceBack(42);
    PLU_CHECK_EQ(added.Value, 42);
    PLU_CHECK_EQ(array.Size(), std::size_t{1});
}

PLU_TEST(DynamicArray_UnqualifiedNameStillResolves)
{
    // DynamicArray moved into namespace Plu; the global using-declaration keeps the
    // unqualified spelling working for the files that have not moved yet.
    DynamicArray<int> global;
    global.PushBack(1);
    Plu::DynamicArray<int>& qualified = global;
    PLU_CHECK_EQ(qualified.Size(), std::size_t{1});
    static_assert(std::is_same_v<DynamicArray<int>, Plu::DynamicArray<int>>);
}

// ============================================================================
// The unified removal family
// ============================================================================

PLU_TEST(DynamicArray_RemoveAtByIndexAndIterator)
{
    Plu::DynamicArray<int> array;
    for (int i = 0; i < 5; ++i) array.PushBack(i); // 0 1 2 3 4

    array.RemoveAt(std::size_t{0});
    PLU_CHECK_EQ(array.Size(), std::size_t{4});
    PLU_CHECK_EQ(array[0], 1);

    array.RemoveAt(array.Begin() + 1); // drops the 2
    PLU_CHECK_EQ(array.Size(), std::size_t{3});
    PLU_CHECK_EQ(array[0], 1);
    PLU_CHECK_EQ(array[1], 3);
    PLU_CHECK_EQ(array[2], 4);
}

PLU_TEST(DynamicArray_RemoveRange)
{
    Plu::DynamicArray<int> array;
    for (int i = 0; i < 10; ++i) array.PushBack(i);

    array.RemoveRange(array.Begin() + 2, array.Begin() + 5); // drops 2 3 4
    PLU_CHECK_EQ(array.Size(), std::size_t{7});
    PLU_CHECK_EQ(array[0], 0);
    PLU_CHECK_EQ(array[1], 1);
    PLU_CHECK_EQ(array[2], 5);
    PLU_CHECK_EQ(array[6], 9);
}

PLU_TEST(DynamicArray_RemoveByValueAndPredicate)
{
    Plu::DynamicArray<int> array;
    for (int i = 0; i < 10; ++i) array.PushBack(i);

    PLU_CHECK(array.Remove(3));
    PLU_CHECK_FALSE(array.Remove(99));
    PLU_CHECK_EQ(array.Size(), std::size_t{9});
    PLU_CHECK_FALSE(array.Contains(3));

    const std::size_t removed = array.RemoveIf([](int v) { return v % 2 == 0; });
    PLU_CHECK_EQ(removed, std::size_t{5}); // 0 2 4 6 8
    PLU_CHECK_EQ(array.Size(), std::size_t{4});
    for (std::size_t i = 0; i < array.Size(); ++i) PLU_CHECK(array[i] % 2 == 1);
}

PLU_TEST(DynamicArray_SwapRemovalIsUnordered)
{
    Plu::DynamicArray<int> array;
    for (int i = 0; i < 5; ++i) array.PushBack(i); // 0 1 2 3 4

    array.RemoveAtSwap(1); // the 4 takes the 1's place
    PLU_CHECK_EQ(array.Size(), std::size_t{4});
    PLU_CHECK_EQ(array[1], 4);
    PLU_CHECK_FALSE(array.Contains(1));

    PLU_CHECK(array.RemoveSwap(0));
    PLU_CHECK_FALSE(array.RemoveSwap(99));
    PLU_CHECK_EQ(array.Size(), std::size_t{3});
}

PLU_TEST(DynamicArray_RemovalDestroysElements)
{
    Tracked::Reset();
    {
        Plu::DynamicArray<Tracked> array;
        for (int i = 0; i < 20; ++i) array.EmplaceBack(i);
        PLU_CHECK_EQ(Tracked::Live, 20);

        array.RemoveAt(std::size_t{0});
        PLU_CHECK_EQ(Tracked::Live, 19);

        array.RemoveRange(array.Begin(), array.Begin() + 5);
        PLU_CHECK_EQ(Tracked::Live, 14);

        array.Clear();
        PLU_CHECK_EQ(Tracked::Live, 0);
    }
    PLU_CHECK_EQ(Tracked::Live, 0);
}

// ============================================================================
// Capacity
// ============================================================================

PLU_TEST(DynamicArray_ReserveResizeShrink)
{
    Plu::DynamicArray<int> array;
    array.Reserve(100);
    PLU_CHECK(array.Capacity() >= 100);
    PLU_CHECK_EQ(array.Size(), std::size_t{0});

    array.Resize(50);
    PLU_CHECK_EQ(array.Size(), std::size_t{50});
    PLU_CHECK_EQ(array[49], 0); // value-initialized

    array.Resize(10);
    PLU_CHECK_EQ(array.Size(), std::size_t{10});

    array.ShrinkToFit();
    PLU_CHECK_EQ(array.Capacity(), std::size_t{10});
    PLU_CHECK_EQ(array.Size(), std::size_t{10});
}

PLU_TEST(DynamicArray_GrowthKeepsContents)
{
    Plu::DynamicArray<Plu::String> array;
    for (int i = 0; i < 500; ++i) array.PushBack(Plu::String::Format("item-{0}", i));

    PLU_CHECK_EQ(array.Size(), std::size_t{500});
    PLU_CHECK(array[0] == Plu::String("item-0"));
    PLU_CHECK(array[499] == Plu::String("item-499"));
}

// ============================================================================
// Value semantics and utilities
// ============================================================================

PLU_TEST(DynamicArray_CopyAndMove)
{
    Plu::DynamicArray<int> original;
    for (int i = 0; i < 10; ++i) original.PushBack(i);

    Plu::DynamicArray<int> copy = original;
    PLU_CHECK(copy == original);
    copy.PushBack(99);
    PLU_CHECK(copy != original);

    Plu::DynamicArray<int> moved = std::move(copy);
    PLU_CHECK_EQ(moved.Size(), std::size_t{11});

    // A moved-from array must still be usable.
    copy.PushBack(1);
    PLU_CHECK_EQ(copy.Size(), std::size_t{1});
}

PLU_TEST(DynamicArray_FindSortAndQueries)
{
    Plu::DynamicArray<int> array;
    for (int value : {5, 3, 9, 1, 7}) array.PushBack(value);

    PLU_CHECK_EQ(array.IndexOf(9), std::size_t{2});
    PLU_CHECK_EQ(array.IndexOf(100), Plu::DynamicArray<int>::InvalidIndex);
    PLU_CHECK(array.Contains(7));
    PLU_CHECK(array.Any([](int v) { return v > 8; }));
    PLU_CHECK(array.All([](int v) { return v > 0; }));
    PLU_CHECK_EQ(array.CountIf([](int v) { return v > 4; }), std::size_t{3});
    PLU_CHECK_EQ(*array.MinElement(), 1);
    PLU_CHECK_EQ(*array.MaxElement(), 9);
    PLU_CHECK_EQ(array.Sum(), 25);

    array.Sort();
    PLU_CHECK_EQ(array[0], 1);
    PLU_CHECK_EQ(array[4], 9);
}

PLU_TEST(DynamicArray_TransformationsAndAppend)
{
    Plu::DynamicArray<int> array;
    for (int i = 1; i <= 5; ++i) array.PushBack(i);

    const auto evens = array.Filter([](int v) { return v % 2 == 0; });
    PLU_CHECK_EQ(evens.Size(), std::size_t{2});

    const auto doubled = array.Map([](int v) { return v * 2; });
    PLU_CHECK_EQ(doubled[4], 10);

    PLU_CHECK_EQ(array.Slice(1, 2).Size(), std::size_t{2});
    PLU_CHECK_EQ(array.First(3).Size(), std::size_t{3});
    // Held in a named value: PLU_CHECK_EQ binds a const reference, and indexing a
    // temporary array hands back a reference into something already destroyed.
    const Plu::DynamicArray<int> lastTwo = array.Last(2);
    PLU_CHECK_EQ(lastTwo[0], 4);
    PLU_CHECK_EQ(array.First(100).Size(), std::size_t{5}); // clamped, not thrown on

    array.Append({6, 7});
    PLU_CHECK_EQ(array.Size(), std::size_t{7});
    PLU_CHECK_EQ(array.Back(), 7);

    PLU_CHECK(array.AddUnique(8));
    PLU_CHECK_FALSE(array.AddUnique(8));
}

PLU_TEST(DynamicArray_InsertAndReverse)
{
    Plu::DynamicArray<int> array;
    for (int i : {1, 2, 4}) array.PushBack(i);

    array.Insert(array.Begin() + 2, 3);
    PLU_CHECK_EQ(array.Size(), std::size_t{4});
    PLU_CHECK_EQ(array[2], 3);
    PLU_CHECK_EQ(array[3], 4);

    array.Reverse();
    PLU_CHECK_EQ(array[0], 4);
    PLU_CHECK_EQ(array[3], 1);
}

PLU_TEST(DynamicArray_SwapExchangesContents)
{
    Plu::DynamicArray<int> a;
    Plu::DynamicArray<int> b;
    a.PushBack(1);
    b.PushBack(2);
    b.PushBack(3);

    a.Swap(b);
    PLU_CHECK_EQ(a.Size(), std::size_t{2});
    PLU_CHECK_EQ(b.Size(), std::size_t{1});
    PLU_CHECK_EQ(a[0], 2);
    PLU_CHECK_EQ(b[0], 1);
}
