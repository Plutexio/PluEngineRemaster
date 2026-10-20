//
// Created by Plutex on 2026-08-07.
//

#include "TestFramework.h"

#include "PluSTL/Queue/Queue.h"
#include "PluSTL/String/String.h"

#include <vector>

// ============================================================================
// Basics
// ============================================================================

PLU_TEST(Queue_StartsEmpty)
{
    Plu::Queue<int> queue;
    PLU_CHECK_EQ(queue.Size(), std::size_t{0});
    PLU_CHECK_EQ(queue.Capacity(), std::size_t{0});
    PLU_CHECK(queue.IsEmpty());

    int value = -1;
    PLU_CHECK_FALSE(queue.TryPopFront(value));
    PLU_CHECK_EQ(value, -1);

    queue.PopFront(); // a no-op on an empty queue, like DynamicArray::PopBack
    PLU_CHECK(queue.IsEmpty());
}

PLU_TEST(Queue_IsFIFO)
{
    Plu::Queue<int> queue;
    for (int i = 0; i < 5; ++i) queue.PushBack(i);

    PLU_CHECK_EQ(queue.Size(), std::size_t{5});
    PLU_CHECK_EQ(queue.Front(), 0);
    PLU_CHECK_EQ(queue.Back(), 4);

    for (int expected = 0; expected < 5; ++expected)
    {
        int value = -1;
        PLU_CHECK(queue.TryPopFront(value));
        PLU_CHECK_EQ(value, expected);
    }
    PLU_CHECK(queue.IsEmpty());
}

PLU_TEST(Queue_IndexingIsLogicalNotPhysical)
{
    // Push and pop past the end of the buffer so the front sits in the middle of it —
    // indices and iterators must still start at the front.
    Plu::Queue<int> queue(4);
    for (int i = 0; i < 4; ++i) queue.PushBack(i);
    for (int i = 0; i < 3; ++i) queue.PopFront(); // head is now physical slot 3
    for (int i = 10; i < 13; ++i) queue.PushBack(i); // wraps into slots 0..1

    PLU_CHECK_EQ(queue.Size(), std::size_t{4});
    PLU_CHECK_EQ(queue.Capacity(), std::size_t{4}); // no growth was needed
    PLU_CHECK_EQ(queue[0], 3);
    PLU_CHECK_EQ(queue[1], 10);
    PLU_CHECK_EQ(queue[3], 12);
    PLU_CHECK_EQ(queue.At(2), 11);
    PLU_CHECK_EQ(queue.Front(), 3);
    PLU_CHECK_EQ(queue.Back(), 12);

    std::vector<int> walked;
    for (int value : queue) walked.push_back(value);
    PLU_CHECK_EQ(walked.size(), std::size_t{4});
    PLU_CHECK_EQ(walked[0], 3);
    PLU_CHECK_EQ(walked[3], 12);
}

PLU_TEST(Queue_GrowthKeepsOrderAcrossAWrap)
{
    Plu::Queue<int> queue(4);
    for (int i = 0; i < 4; ++i) queue.PushBack(i);
    for (int i = 0; i < 2; ++i) queue.PopFront();
    for (int i = 100; i < 104; ++i) queue.PushBack(i); // forces a wrap, then growth

    PLU_CHECK(queue.Capacity() > 4);
    PLU_CHECK_EQ(queue.Size(), std::size_t{6});

    const int expected[] = {2, 3, 100, 101, 102, 103};
    for (std::size_t i = 0; i < queue.Size(); ++i)
        PLU_CHECK_EQ(queue[i], expected[i]);
}

PLU_TEST(Queue_LongRunOfPushPopStaysCorrect)
{
    // Walk the ring around many times: the buffer must be reused, not grown.
    Plu::Queue<int> queue;
    queue.Reserve(8);
    const std::size_t capacity = queue.Capacity();

    int nextToPush = 0;
    int nextToPop = 0;
    for (int round = 0; round < 1000; ++round)
    {
        queue.PushBack(nextToPush++);
        queue.PushBack(nextToPush++);

        int value = -1;
        PLU_CHECK(queue.TryPopFront(value));
        PLU_CHECK_EQ(value, nextToPop++);
    }

    PLU_CHECK_EQ(queue.Size(), static_cast<std::size_t>(nextToPush - nextToPop));
    PLU_CHECK_EQ(queue.Front(), nextToPop);
    PLU_CHECK(queue.Capacity() >= capacity);
}

// ============================================================================
// The DynamicArray-shaped part of the API
// ============================================================================

PLU_TEST(Queue_ReserveClearShrink)
{
    Plu::Queue<int> queue;
    queue.Reserve(64);
    PLU_CHECK_EQ(queue.Capacity(), std::size_t{64});
    PLU_CHECK(queue.IsEmpty());

    queue.Reserve(8); // never shrinks
    PLU_CHECK_EQ(queue.Capacity(), std::size_t{64});

    for (int i = 0; i < 10; ++i) queue.PushBack(i);
    queue.ShrinkToFit();
    PLU_CHECK_EQ(queue.Capacity(), std::size_t{10});
    PLU_CHECK_EQ(queue.Front(), 0);
    PLU_CHECK_EQ(queue.Back(), 9);

    queue.Clear();
    PLU_CHECK(queue.IsEmpty());
    PLU_CHECK_EQ(queue.Size(), std::size_t{0});

    queue.PushBack(42); // usable again
    PLU_CHECK_EQ(queue.Front(), 42);
}

PLU_TEST(Queue_FindContainsIndexOf)
{
    Plu::Queue<int> queue{0, 1, 2, 3, 4};
    queue.PopFront(); // 1 2 3 4

    PLU_CHECK(queue.Contains(3));
    PLU_CHECK_FALSE(queue.Contains(0));
    PLU_CHECK_EQ(queue.IndexOf(3), std::size_t{2});
    PLU_CHECK_EQ(queue.IndexOf(99), queue.InvalidIndex);

    PLU_CHECK(queue.Find(4) != queue.End());
    PLU_CHECK(queue.Find(99) == queue.End());
    PLU_CHECK_EQ(*queue.Find(2), 2);
    PLU_CHECK_EQ(queue.Find(2) - queue.Begin(), std::ptrdiff_t{1});

    Plu::Queue<int>::Iterator it = queue.FindIf([](const int& v) { return v > 2; });
    PLU_CHECK(it != queue.End());
    PLU_CHECK_EQ(*it, 3);
}

PLU_TEST(Queue_SwapExchangesContents)
{
    Plu::Queue<int> a{1, 2, 3};
    Plu::Queue<int> b{9};

    a.Swap(b);

    PLU_CHECK_EQ(a.Size(), std::size_t{1});
    PLU_CHECK_EQ(a.Front(), 9);
    PLU_CHECK_EQ(b.Size(), std::size_t{3});
    PLU_CHECK_EQ(b.Front(), 1);
    PLU_CHECK_EQ(b.Back(), 3);
}

// ============================================================================
// Value semantics and non-trivial elements
// ============================================================================

PLU_TEST(Queue_CopyAndMove)
{
    Plu::Queue<Plu::String> original;
    original.PushBack(Plu::String("a"));
    original.PushBack(Plu::String("b"));
    original.PopFront();
    original.PushBack(Plu::String("c")); // wrapped: b c

    Plu::Queue<Plu::String> copy = original;
    PLU_CHECK_EQ(copy.Size(), std::size_t{2});
    PLU_CHECK(copy[0] == Plu::String("b"));
    PLU_CHECK(copy[1] == Plu::String("c"));

    copy.PushBack(Plu::String("d"));
    PLU_CHECK_EQ(original.Size(), std::size_t{2}); // a real copy, not a view

    Plu::Queue<Plu::String> moved = static_cast<Plu::Queue<Plu::String>&&>(copy);
    PLU_CHECK_EQ(moved.Size(), std::size_t{3});
    PLU_CHECK(moved.Back() == Plu::String("d"));
    PLU_CHECK(copy.IsEmpty());
    PLU_CHECK_EQ(copy.Capacity(), std::size_t{0});

    Plu::Queue<Plu::String> assigned;
    assigned.PushBack(Plu::String("stale"));
    assigned = moved;
    PLU_CHECK_EQ(assigned.Size(), std::size_t{3});
    PLU_CHECK(assigned.Front() == Plu::String("b"));
}

PLU_TEST(Queue_EmplaceBackConstructsInPlace)
{
    Plu::Queue<Plu::String> queue;

    Plu::String& first = queue.EmplaceBack("hello");
    PLU_CHECK(first == Plu::String("hello"));
    queue.EmplaceBack("world");

    PLU_CHECK_EQ(queue.Size(), std::size_t{2});
    PLU_CHECK(queue.Front() == Plu::String("hello"));
    PLU_CHECK(queue.Back() == Plu::String("world"));

    Plu::String popped;
    PLU_CHECK(queue.TryPopFront(popped));
    PLU_CHECK(popped == Plu::String("hello"));
    PLU_CHECK(queue.Front() == Plu::String("world"));
}

PLU_TEST(Queue_DestroysEveryElementItStillHolds)
{
    // The destructor and Clear() must walk the ring, not slots 0..Size.
    struct Counted
    {
        static int& LiveCount() { static int count = 0; return count; }

        Counted() { ++LiveCount(); }
        Counted(const Counted&) { ++LiveCount(); }
        Counted(Counted&&) noexcept { ++LiveCount(); }
        Counted& operator=(const Counted&) = default;
        Counted& operator=(Counted&&) noexcept = default;
        ~Counted() { --LiveCount(); }
    };

    Counted::LiveCount() = 0;
    {
        Plu::Queue<Counted> queue(4);
        for (int i = 0; i < 4; ++i) queue.EmplaceBack();
        for (int i = 0; i < 3; ++i) queue.PopFront();
        for (int i = 0; i < 3; ++i) queue.EmplaceBack(); // wrapped, 4 live

        PLU_CHECK_EQ(Counted::LiveCount(), 4);

        queue.Clear();
        PLU_CHECK_EQ(Counted::LiveCount(), 0);

        for (int i = 0; i < 5; ++i) queue.EmplaceBack();
        PLU_CHECK_EQ(Counted::LiveCount(), 5);
    }
    PLU_CHECK_EQ(Counted::LiveCount(), 0);
}
