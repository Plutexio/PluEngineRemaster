//
// Created by Plutex on 2026-08-06.
//

#include "TestFramework.h"

#include "PluSTL/Concurrent/ConcurrentHashSet.h"
#include "PluSTL/String/String.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

// ============================================================================
// Single-threaded correctness
// ============================================================================

PLU_TEST(ConcurrentHashSet_StartsEmpty)
{
    Plu::ConcurrentHashSet<std::uint64_t> set;
    PLU_CHECK_EQ(set.Size(), std::size_t{0});
    PLU_CHECK(set.IsEmpty());
    PLU_CHECK_FALSE(set.Contains(1));
    PLU_CHECK_FALSE(set.Remove(1));
}

PLU_TEST(ConcurrentHashSet_InsertIsTheDedupePrimitive)
{
    Plu::ConcurrentHashSet<std::uint64_t> set;
    PLU_CHECK(set.Insert(42));       // true = added
    PLU_CHECK_FALSE(set.Insert(42)); // false = already there
    PLU_CHECK_EQ(set.Size(), std::size_t{1});
    PLU_CHECK(set.Contains(42));
}

PLU_TEST(ConcurrentHashSet_Remove)
{
    Plu::ConcurrentHashSet<std::uint64_t> set;
    set.Insert(1);
    set.Insert(2);
    set.Insert(3);

    PLU_CHECK(set.Remove(2));
    PLU_CHECK_FALSE(set.Remove(2));
    PLU_CHECK_EQ(set.Size(), std::size_t{2});
    PLU_CHECK_FALSE(set.Contains(2));

    // Removing then re-inserting must work (the node was really freed, not just unlinked).
    PLU_CHECK(set.Insert(2));
    PLU_CHECK_EQ(set.Size(), std::size_t{3});
}

PLU_TEST(ConcurrentHashSet_GrowthKeepsEveryElement)
{
    Plu::ConcurrentHashSet<std::uint64_t> set;
    constexpr std::uint64_t kCount = 5000;

    const std::size_t initialBuckets = set.BucketCount();
    for (std::uint64_t i = 0; i < kCount; ++i) PLU_CHECK(set.Insert(i));

    PLU_CHECK(set.BucketCount() > initialBuckets);
    PLU_CHECK_EQ(set.Size(), static_cast<std::size_t>(kCount));
    for (std::uint64_t i = 0; i < kCount; ++i) PLU_CHECK(set.Contains(i));
}

PLU_TEST(ConcurrentHashSet_ForEach)
{
    Plu::ConcurrentHashSet<std::uint64_t> set;
    for (std::uint64_t i = 0; i < 400; ++i) set.Insert(i);

    std::uint64_t sum = 0;
    std::size_t visited = 0;
    set.ForEach([&](const std::uint64_t& value) { sum += value; ++visited; });

    PLU_CHECK_EQ(visited, std::size_t{400});
    PLU_CHECK_EQ(sum, std::uint64_t{399} * 400 / 2);
}

PLU_TEST(ConcurrentHashSet_Clear)
{
    Plu::ConcurrentHashSet<std::uint64_t> set;
    for (std::uint64_t i = 0; i < 300; ++i) set.Insert(i);

    set.Clear();
    PLU_CHECK_EQ(set.Size(), std::size_t{0});
    PLU_CHECK_FALSE(set.Contains(0));
    PLU_CHECK(set.Insert(0)); // usable again
}

PLU_TEST(ConcurrentHashSet_DrainToArrayEmptiesAndReturnsEverything)
{
    Plu::ConcurrentHashSet<std::uint64_t> set;
    constexpr std::uint64_t kCount = 1000;
    for (std::uint64_t i = 0; i < kCount; ++i) set.Insert(i);

    DynamicArray<std::uint64_t> drained = set.DrainToArray();

    PLU_CHECK_EQ(drained.Size(), static_cast<std::size_t>(kCount));
    PLU_CHECK_EQ(set.Size(), std::size_t{0});
    PLU_CHECK(set.IsEmpty());

    // Exactly the union of what went in, each element once.
    std::vector<bool> seen(kCount, false);
    for (std::uint64_t value : drained)
    {
        PLU_CHECK(value < kCount);
        if (value < kCount)
        {
            PLU_CHECK_FALSE(seen[value]); // no duplicates
            seen[value] = true;
        }
    }
    for (std::uint64_t i = 0; i < kCount; ++i) PLU_CHECK(seen[i]);

    // Draining an empty set is fine and gives an empty array.
    DynamicArray<std::uint64_t> empty = set.DrainToArray();
    PLU_CHECK_EQ(empty.Size(), std::size_t{0});
}

// ============================================================================
// The HashSet-shaped part of the API
// ============================================================================

PLU_TEST(ConcurrentHashSet_EmplaceIsInsertWithInPlaceConstruction)
{
    Plu::ConcurrentHashSet<Plu::String> set;

    PLU_CHECK(set.Emplace("hello"));
    PLU_CHECK_FALSE(set.Emplace("hello"));
    PLU_CHECK_EQ(set.Size(), std::size_t{1});
    PLU_CHECK(set.Contains(Plu::String("hello")));
}

PLU_TEST(ConcurrentHashSet_ReserveAndRehashKeepEveryElement)
{
    Plu::ConcurrentHashSet<int> set;
    for (int i = 0; i < 100; ++i) set.Insert(i);

    set.Reserve(4000);
    PLU_CHECK(set.BucketCount() >= 4000);
    PLU_CHECK_EQ(set.Size(), std::size_t{100});

    set.Rehash(8); // clamped to the stripe count and to what the load factor needs
    PLU_CHECK(set.BucketCount() >= set.kMinBucketCount);
    PLU_CHECK(set.LoadFactor() <= set.kMaxLoadFactor);

    for (int i = 0; i < 100; ++i) PLU_CHECK(set.Contains(i));
}

PLU_TEST(ConcurrentHashSet_SnapshotIsAPlainHashSet)
{
    Plu::ConcurrentHashSet<int> set;
    for (int i = 0; i < 200; ++i) set.Insert(i);

    Plu::HashSet<int> snapshot = set.Snapshot();
    PLU_CHECK_EQ(snapshot.Size(), std::size_t{200});
    for (int i = 0; i < 200; ++i) PLU_CHECK(snapshot.Contains(i));

    // A copy, not a view: the original keeps its contents.
    PLU_CHECK_EQ(set.Size(), std::size_t{200});
}

// ============================================================================
// Stress
// ============================================================================

PLU_TEST(ConcurrentHashSet_Stress_InsertedExactlyOnce)
{
    // All threads insert the same range. The true-returns must sum to the range size:
    // that is the guarantee RequestAssetDataLoad relies on for dedupe.
    Plu::ConcurrentHashSet<std::uint64_t> set;
    const unsigned int threadCount = PluTest::StressThreadCount();
    constexpr std::uint64_t kKeyCount = 5000;

    std::atomic<int> creations{0};
    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (unsigned int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&]
        {
            int local = 0;
            for (std::uint64_t i = 0; i < kKeyCount; ++i)
                if (set.Insert(i)) ++local;
            creations.fetch_add(local, std::memory_order_relaxed);
        });
    }
    for (std::thread& thread : threads) thread.join();

    PLU_CHECK_EQ(creations.load(), static_cast<int>(kKeyCount));
    PLU_CHECK_EQ(set.Size(), static_cast<std::size_t>(kKeyCount));
}

PLU_TEST(ConcurrentHashSet_Stress_ProducersAndOneDrainer)
{
    // The mPendingLoadRequests shape: producers post work items while a single consumer
    // drains in batches. Each producer owns a disjoint value range, so every value is
    // inserted at most once for its whole life — which makes "delivered exactly once" a
    // real invariant. It is the property the old copy-then-clear code could not offer:
    // there, a value inserted between the copy and the clear was silently dropped.
    Plu::ConcurrentHashSet<std::uint64_t> set;
    const unsigned int producerCount = PluTest::StressThreadCount();
    constexpr std::uint64_t kPerProducer = 20000;
    const std::uint64_t kValueCount = kPerProducer * producerCount;

    std::atomic<unsigned int> producersDone{0};
    std::vector<int> seenCount(kValueCount, 0);

    std::vector<std::thread> producers;
    producers.reserve(producerCount);
    for (unsigned int p = 0; p < producerCount; ++p)
    {
        producers.emplace_back([&, p]
        {
            const std::uint64_t base = static_cast<std::uint64_t>(p) * kPerProducer;
            for (std::uint64_t i = 0; i < kPerProducer; ++i) set.Insert(base + i);
            producersDone.fetch_add(1, std::memory_order_release);
        });
    }

    std::thread drainer([&]
    {
        DynamicArray<std::uint64_t> batch;
        while (producersDone.load(std::memory_order_acquire) < producerCount)
        {
            batch = set.DrainToArray();
            for (std::uint64_t value : batch) ++seenCount[value];
        }
        // Final drain, after every producer has finished.
        batch = set.DrainToArray();
        for (std::uint64_t value : batch) ++seenCount[value];
    });

    for (std::thread& thread : producers) thread.join();
    drainer.join();

    PLU_CHECK_EQ(set.Size(), std::size_t{0});
    for (std::uint64_t i = 0; i < kValueCount; ++i)
    {
        // Each value must have been delivered exactly once — never lost, never duplicated.
        PLU_CHECK_EQ(seenCount[i], 1);
    }
}

PLU_TEST(ConcurrentHashSet_Stress_MixedInsertRemoveContains)
{
    Plu::ConcurrentHashSet<std::uint64_t> set;
    const unsigned int threadCount = PluTest::StressThreadCount();
    constexpr std::uint64_t kKeyCount = 2000;
    constexpr int kRounds = 100;

    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (unsigned int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&set, t]
        {
            for (int round = 0; round < kRounds; ++round)
            {
                for (std::uint64_t key = 0; key < kKeyCount; ++key)
                {
                    if ((key + t) % 3 == 0) set.Insert(key);
                    else if ((key + t) % 3 == 1) set.Remove(key);
                    else (void)set.Contains(key);
                }
            }
        });
    }
    for (std::thread& thread : threads) thread.join();

    std::size_t counted = 0;
    set.ForEach([&counted](const std::uint64_t&) { ++counted; });
    PLU_CHECK_EQ(set.Size(), counted);
}
