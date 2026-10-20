//
// Created by Plutex on 2026-08-06.
//

#include "TestFramework.h"

#include "PluSTL/Concurrent/ConcurrentArray.h"
#include "PluSTL/String/String.h"

#include <atomic>
#include <thread>
#include <vector>

// ============================================================================
// Single-threaded correctness
// ============================================================================

PLU_TEST(ConcurrentArray_StartsEmpty)
{
    Plu::ConcurrentArray<int> array;
    PLU_CHECK_EQ(array.Size(), std::size_t{0});
    PLU_CHECK(array.IsEmpty());

    int value = -1;
    PLU_CHECK_FALSE(array.Get(0, value));
    PLU_CHECK_EQ(value, -1);
}

PLU_TEST(ConcurrentArray_PushReturnsTheIndex)
{
    Plu::ConcurrentArray<int> array;
    PLU_CHECK_EQ(array.PushBack(10), std::size_t{0});
    PLU_CHECK_EQ(array.PushBack(20), std::size_t{1});
    PLU_CHECK_EQ(array.PushBack(30), std::size_t{2});
    PLU_CHECK_EQ(array.Size(), std::size_t{3});

    int value = 0;
    PLU_CHECK(array.Get(1, value));
    PLU_CHECK_EQ(value, 20);
    PLU_CHECK_FALSE(array.Get(3, value)); // one past the end
}

PLU_TEST(ConcurrentArray_Visit)
{
    Plu::ConcurrentArray<int> array;
    array.PushBack(1);
    array.PushBack(2);

    PLU_CHECK(array.Visit(0, [](int& value) { value = 100; }));
    PLU_CHECK_FALSE(array.Visit(5, [](int&) { PLU_CHECK(false); }));

    int value = 0;
    array.Get(0, value);
    PLU_CHECK_EQ(value, 100);
}

PLU_TEST(ConcurrentArray_ForEachIsIndexed)
{
    Plu::ConcurrentArray<int> array;
    for (int i = 0; i < 100; ++i) array.PushBack(i * 2);

    std::size_t visited = 0;
    bool indicesMatch = true;
    array.ForEach([&](std::size_t index, const int& value)
    {
        if (value != static_cast<int>(index) * 2) indicesMatch = false;
        ++visited;
    });

    PLU_CHECK_EQ(visited, std::size_t{100});
    PLU_CHECK(indicesMatch);
}

PLU_TEST(ConcurrentArray_AddressesAreStableAcrossChunkGrowth)
{
    // The whole point of the type: an element's address must survive every later push,
    // including the ones that allocate new chunks. DynamicArray cannot promise this.
    Plu::ConcurrentArray<int, 8> array; // tiny chunks so growth happens constantly

    const int* firstAddress = nullptr;
    array.PushBack(1);
    array.Visit(0, [&](int& value) { firstAddress = &value; });

    for (int i = 1; i < 500; ++i) array.PushBack(i);

    const int* firstAddressAgain = nullptr;
    array.Visit(0, [&](int& value) { firstAddressAgain = &value; });

    PLU_CHECK(firstAddress == firstAddressAgain);
    PLU_CHECK_EQ(array.Size(), std::size_t{500});

    for (std::size_t i = 0; i < array.Size(); ++i)
    {
        int value = -1;
        PLU_CHECK(array.Get(i, value));
        PLU_CHECK_EQ(value, static_cast<int>(i == 0 ? 1 : i));
    }
}

PLU_TEST(ConcurrentArray_EmplaceAndNonTrivialElements)
{
    Plu::ConcurrentArray<Plu::String, 4> array;
    for (int i = 0; i < 100; ++i)
        PLU_CHECK_EQ(array.EmplaceBack(Plu::String("item_") + Plu::String::FromInt(i)),
                     static_cast<std::size_t>(i));

    Plu::String value;
    PLU_CHECK(array.Get(42, value));
    PLU_CHECK(value == Plu::String("item_42"));
}

PLU_TEST(ConcurrentArray_Clear)
{
    Plu::ConcurrentArray<Plu::String, 4> array;
    for (int i = 0; i < 50; ++i) array.EmplaceBack(Plu::String("x"));

    array.Clear();
    PLU_CHECK_EQ(array.Size(), std::size_t{0});

    Plu::String value;
    PLU_CHECK_FALSE(array.Get(0, value));

    // Usable again — indices restart from 0.
    PLU_CHECK_EQ(array.PushBack(Plu::String("y")), std::size_t{0});
    PLU_CHECK(array.Get(0, value));
    PLU_CHECK(value == Plu::String("y"));
    PLU_CHECK_EQ(array.Capacity(), std::size_t{4}); // one chunk back
}

// ============================================================================
// The DynamicArray-shaped part of the API
// ============================================================================

PLU_TEST(ConcurrentArray_ReserveAllocatesChunksUpFront)
{
    Plu::ConcurrentArray<int, 8, 16> array;
    PLU_CHECK_EQ(array.Capacity(), std::size_t{0});
    PLU_CHECK_EQ(array.MaxCapacity(), std::size_t{128});

    array.Reserve(20); // 3 chunks of 8
    PLU_CHECK_EQ(array.Capacity(), std::size_t{24});
    PLU_CHECK_EQ(array.Size(), std::size_t{0});
    PLU_CHECK(array.IsEmpty());

    for (int i = 0; i < 24; ++i) array.PushBack(i);
    PLU_CHECK_EQ(array.Capacity(), std::size_t{24}); // nothing new was needed

    array.PushBack(24);
    PLU_CHECK_EQ(array.Capacity(), std::size_t{32});

    array.Reserve(4); // never shrinks
    PLU_CHECK_EQ(array.Capacity(), std::size_t{32});

    array.Reserve(9999); // capped at MaxCapacity
    PLU_CHECK_EQ(array.Capacity(), array.MaxCapacity());
}

PLU_TEST(ConcurrentArray_IndexOfContainsFrontBack)
{
    Plu::ConcurrentArray<int, 4> array;

    int value = -1;
    PLU_CHECK_FALSE(array.Front(value));
    PLU_CHECK_FALSE(array.Back(value));

    for (int i = 0; i < 10; ++i) array.PushBack(i * 3);

    PLU_CHECK_EQ(array.IndexOf(0), std::size_t{0});
    PLU_CHECK_EQ(array.IndexOf(27), std::size_t{9});
    PLU_CHECK_EQ(array.IndexOf(4), array.InvalidIndex);
    PLU_CHECK(array.Contains(15));
    PLU_CHECK_FALSE(array.Contains(16));

    PLU_CHECK_EQ(array.IndexOfIf([](const int& v) { return v > 10; }), std::size_t{4});
    PLU_CHECK_EQ(array.IndexOfIf([](const int& v) { return v < 0; }), array.InvalidIndex);

    PLU_CHECK(array.Front(value));
    PLU_CHECK_EQ(value, 0);
    PLU_CHECK(array.Back(value));
    PLU_CHECK_EQ(value, 27);
}

// ============================================================================
// Stress
// ============================================================================

PLU_TEST(ConcurrentArray_Stress_ConcurrentPushGivesUniqueIndices)
{
    // Every thread pushes its own tagged values. Indices must be handed out exactly once
    // (no two elements share one) and Size() must equal the total pushed.
    // Chunk size picked so the table holds threadCount * kPerThread with room to spare —
    // ConcurrentArray is bounded by ChunkSize * MaxChunks by design.
    Plu::ConcurrentArray<int, 512> array;
    const unsigned int threadCount = PluTest::StressThreadCount();
    constexpr int kPerThread = 5000;

    std::vector<std::vector<std::size_t>> indicesPerThread(threadCount);

    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (unsigned int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&, t]
        {
            indicesPerThread[t].reserve(kPerThread);
            for (int i = 0; i < kPerThread; ++i)
                indicesPerThread[t].push_back(array.PushBack(static_cast<int>(t)));
        });
    }
    for (std::thread& thread : threads) thread.join();

    const std::size_t total = static_cast<std::size_t>(threadCount) * kPerThread;
    PLU_CHECK_EQ(array.Size(), total);

    std::vector<int> indexUseCount(total, 0);
    for (unsigned int t = 0; t < threadCount; ++t)
    {
        for (std::size_t index : indicesPerThread[t])
        {
            PLU_CHECK(index < total);
            if (index < total) ++indexUseCount[index];
        }
    }

    bool everyIndexUsedOnce = true;
    for (int count : indexUseCount) if (count != 1) everyIndexUsedOnce = false;
    PLU_CHECK(everyIndexUsedOnce);

    // And the value stored at each returned index must be the tag of the thread that got it.
    bool valuesMatch = true;
    for (unsigned int t = 0; t < threadCount; ++t)
    {
        for (std::size_t index : indicesPerThread[t])
        {
            int value = -1;
            if (!array.Get(index, value) || value != static_cast<int>(t)) valuesMatch = false;
        }
    }
    PLU_CHECK(valuesMatch);
}

PLU_TEST(ConcurrentArray_Stress_ReadersSeeOnlyPublishedElements)
{
    // Size() is a committed prefix: anything below it must be fully constructed. A reader
    // sweeping [0, Size()) concurrently with writers must never see a half-built element.
    Plu::ConcurrentArray<int, 256> array;
    constexpr int kCount = 100000;

    std::atomic<bool> writersDone{false};
    std::atomic<int> badReads{0};

    const unsigned int writerCount = PluTest::StressThreadCount() / 2 + 1;
    std::vector<std::thread> writers;
    writers.reserve(writerCount);
    std::atomic<unsigned int> writersFinished{0};
    for (unsigned int w = 0; w < writerCount; ++w)
    {
        writers.emplace_back([&]
        {
            for (int i = 0; i < kCount / static_cast<int>(writerCount); ++i)
                array.PushBack(0xABCD);
            if (writersFinished.fetch_add(1, std::memory_order_acq_rel) + 1 == writerCount)
                writersDone.store(true, std::memory_order_release);
        });
    }

    std::thread reader([&]
    {
        while (!writersDone.load(std::memory_order_acquire))
        {
            const std::size_t size = array.Size();
            for (std::size_t i = 0; i < size; i += 37)
            {
                int value = 0;
                if (!array.Get(i, value) || value != 0xABCD)
                    badReads.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    for (std::thread& thread : writers) thread.join();
    reader.join();

    PLU_CHECK_EQ(badReads.load(), 0);
}

PLU_TEST(ConcurrentArray_Stress_VisitOnDisjointIndices)
{
    // Stable addresses mean in-place mutation needs no lock as long as threads touch
    // different indices — the slot-map access pattern.
    Plu::ConcurrentArray<long long, 64> array;
    constexpr int kCount = 20000;
    for (int i = 0; i < kCount; ++i) array.PushBack(0);

    const unsigned int threadCount = PluTest::StressThreadCount();
    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (unsigned int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&, t]
        {
            for (std::size_t i = t; i < kCount; i += threadCount)
                array.Visit(i, [i](long long& value) { value = static_cast<long long>(i) * 3; });
        });
    }
    for (std::thread& thread : threads) thread.join();

    bool allWritten = true;
    array.ForEach([&](std::size_t index, const long long& value)
    {
        if (value != static_cast<long long>(index) * 3) allWritten = false;
    });
    PLU_CHECK(allWritten);
}
