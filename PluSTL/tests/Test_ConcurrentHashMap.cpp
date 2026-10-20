//
// Created by Plutex on 2026-08-06.
//

#include "TestFramework.h"

#include "PluSTL/Concurrent/ConcurrentHashMap.h"
#include "PluSTL/String/String.h"

#include <atomic>
#include <thread>
#include <vector>

// ============================================================================
// Single-threaded correctness — mirrors HashMap semantics
// ============================================================================

PLU_TEST(ConcurrentHashMap_StartsEmpty)
{
    Plu::ConcurrentHashMap<int, int> map;
    PLU_CHECK_EQ(map.Size(), std::size_t{0});
    PLU_CHECK(map.IsEmpty());
    PLU_CHECK_FALSE(map.Contains(1));

    int value = -1;
    PLU_CHECK_FALSE(map.Find(1, value));
    PLU_CHECK_EQ(value, -1); // untouched on a miss
}

PLU_TEST(ConcurrentHashMap_InsertRejectsDuplicateKeys)
{
    Plu::ConcurrentHashMap<int, int> map;
    PLU_CHECK(map.Insert(7, 70));
    PLU_CHECK_FALSE(map.Insert(7, 99));
    PLU_CHECK_EQ(map.Size(), std::size_t{1});

    int value = 0;
    PLU_CHECK(map.Find(7, value));
    PLU_CHECK_EQ(value, 70); // the rejected insert must not have overwritten anything
}

PLU_TEST(ConcurrentHashMap_InsertOrAssignOverwrites)
{
    Plu::ConcurrentHashMap<int, int> map;
    PLU_CHECK(map.InsertOrAssign(7, 70));   // true = created
    PLU_CHECK_FALSE(map.InsertOrAssign(7, 71)); // false = replaced an existing entry
    PLU_CHECK_EQ(map.Size(), std::size_t{1});

    int value = 0;
    PLU_CHECK(map.Find(7, value));
    PLU_CHECK_EQ(value, 71);
}

PLU_TEST(ConcurrentHashMap_Remove)
{
    Plu::ConcurrentHashMap<int, int> map;
    map.Insert(1, 10);
    map.Insert(2, 20);
    map.Insert(3, 30);

    PLU_CHECK(map.Remove(2));
    PLU_CHECK_FALSE(map.Remove(2)); // already gone
    PLU_CHECK_EQ(map.Size(), std::size_t{2});
    PLU_CHECK_FALSE(map.Contains(2));
    PLU_CHECK(map.Contains(1));
    PLU_CHECK(map.Contains(3));
}

PLU_TEST(ConcurrentHashMap_Visit)
{
    Plu::ConcurrentHashMap<int, int> map;
    map.Insert(5, 50);

    PLU_CHECK(map.Visit(5, [](int& value) { value += 1; }));
    PLU_CHECK_FALSE(map.Visit(6, [](int&) { PLU_CHECK(false); }));

    int value = 0;
    map.Find(5, value);
    PLU_CHECK_EQ(value, 51);
}

PLU_TEST(ConcurrentHashMap_VisitOrInsertCreatesThenAccumulates)
{
    Plu::ConcurrentHashMap<int, int> map;

    // First call creates the entry from the default and then runs the callback on it.
    map.VisitOrInsert(1, [](int& value) { value += 5; }, 100);
    int value = 0;
    PLU_CHECK(map.Find(1, value));
    PLU_CHECK_EQ(value, 105);

    // Second call must find the existing entry, not reset it to the default.
    map.VisitOrInsert(1, [](int& v) { v += 5; }, 100);
    map.Find(1, value);
    PLU_CHECK_EQ(value, 110);
    PLU_CHECK_EQ(map.Size(), std::size_t{1});
}

PLU_TEST(ConcurrentHashMap_Clear)
{
    Plu::ConcurrentHashMap<int, int> map;
    for (int i = 0; i < 200; ++i) map.Insert(i, i * 2);
    PLU_CHECK_EQ(map.Size(), std::size_t{200});

    map.Clear();
    PLU_CHECK_EQ(map.Size(), std::size_t{0});
    PLU_CHECK_FALSE(map.Contains(0));
    PLU_CHECK_FALSE(map.Contains(199));

    // Usable again after a Clear.
    PLU_CHECK(map.Insert(1, 1));
    PLU_CHECK_EQ(map.Size(), std::size_t{1});
}

PLU_TEST(ConcurrentHashMap_GrowthKeepsEveryEntry)
{
    Plu::ConcurrentHashMap<int, int> map;
    constexpr int kCount = 5000;

    const std::size_t initialBuckets = map.BucketCount();
    for (int i = 0; i < kCount; ++i) PLU_CHECK(map.Insert(i, i * 3));

    PLU_CHECK(map.BucketCount() > initialBuckets); // it really did rehash
    PLU_CHECK_EQ(map.Size(), static_cast<std::size_t>(kCount));

    for (int i = 0; i < kCount; ++i)
    {
        int value = -1;
        PLU_CHECK(map.Find(i, value));
        PLU_CHECK_EQ(value, i * 3);
    }
}

PLU_TEST(ConcurrentHashMap_ForEachAndSnapshot)
{
    Plu::ConcurrentHashMap<int, int> map;
    for (int i = 0; i < 500; ++i) map.Insert(i, i + 1000);

    long long sum = 0;
    std::size_t visited = 0;
    map.ForEach([&](const int&, const int& value) { sum += value; ++visited; });
    PLU_CHECK_EQ(visited, std::size_t{500});
    PLU_CHECK_EQ(sum, 500LL * 1000 + (499LL * 500 / 2));

    Plu::HashMap<int, int> snapshot = map.Snapshot();
    PLU_CHECK_EQ(snapshot.Size(), std::size_t{500});
    for (int i = 0; i < 500; ++i)
    {
        const int* found = snapshot.Find(i);
        PLU_CHECK(found != nullptr);
        if (found) PLU_CHECK_EQ(*found, i + 1000);
    }
}

PLU_TEST(ConcurrentHashMap_StringKeys)
{
    Plu::ConcurrentHashMap<Plu::String, int> map;

    for (int i = 0; i < 300; ++i)
        PLU_CHECK(map.Insert(Plu::String("key_") + Plu::String::FromInt(i), i));

    PLU_CHECK_EQ(map.Size(), std::size_t{300});

    int value = -1;
    PLU_CHECK(map.Find(Plu::String("key_42"), value));
    PLU_CHECK_EQ(value, 42);
    PLU_CHECK_FALSE(map.Find(Plu::String("key_9999"), value));
}

// ============================================================================
// The HashMap-shaped part of the API
// ============================================================================

PLU_TEST(ConcurrentHashMap_EmplaceConstructsInPlaceAndKeepsExisting)
{
    Plu::ConcurrentHashMap<int, Plu::String> map;

    PLU_CHECK(map.Emplace(1, "hello"));
    PLU_CHECK_FALSE(map.Emplace(1, "other")); // key already there — like HashMap
    PLU_CHECK_EQ(map.Size(), std::size_t{1});

    Plu::String value;
    PLU_CHECK(map.Find(1, value));
    PLU_CHECK(value == Plu::String("hello"));
}

PLU_TEST(ConcurrentHashMap_FindOr)
{
    Plu::ConcurrentHashMap<int, int> map;
    map.Insert(7, 70);

    PLU_CHECK_EQ(map.FindOr(7, -1), 70);
    PLU_CHECK_EQ(map.FindOr(8, -1), -1);
    PLU_CHECK_EQ(map.Size(), std::size_t{1}); // a miss must not insert
}

PLU_TEST(ConcurrentHashMap_ReserveAndRehashKeepEveryEntry)
{
    Plu::ConcurrentHashMap<int, int> map;
    for (int i = 0; i < 100; ++i) map.Insert(i, i * 2);

    map.Reserve(4000);
    PLU_CHECK(map.BucketCount() >= 4000);
    PLU_CHECK_EQ(map.Size(), std::size_t{100});

    map.Rehash(8); // clamped to the stripe count and to what the load factor needs
    PLU_CHECK(map.BucketCount() >= map.kMinBucketCount);
    PLU_CHECK(map.LoadFactor() <= map.kMaxLoadFactor);

    for (int i = 0; i < 100; ++i)
    {
        int value = -1;
        PLU_CHECK(map.Find(i, value));
        PLU_CHECK_EQ(value, i * 2);
    }
}

PLU_TEST(ConcurrentHashMap_DrainEmptiesAndHandsOverEverything)
{
    Plu::ConcurrentHashMap<int, Plu::String> map;
    for (int i = 0; i < 200; ++i) map.Insert(i, Plu::String::FromInt(i));

    long long keySum = 0;
    std::size_t drained = 0;
    map.Drain([&](const int& key, Plu::String&& value)
    {
        PLU_CHECK(value == Plu::String::FromInt(key));
        keySum += key;
        ++drained;
    });

    PLU_CHECK_EQ(drained, std::size_t{200});
    PLU_CHECK_EQ(keySum, 199LL * 200 / 2);
    PLU_CHECK_EQ(map.Size(), std::size_t{0});
    PLU_CHECK(map.IsEmpty());
}

// ============================================================================
// Stress — this is what TSan is here for
// ============================================================================

PLU_TEST(ConcurrentHashMap_Stress_DisjointKeysPerThread)
{
    // N threads insert their own key range. Nothing may be lost, and every value must
    // read back exactly as written.
    Plu::ConcurrentHashMap<int, int> map;
    const unsigned int threadCount = PluTest::StressThreadCount();
    constexpr int kPerThread = 4000;

    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (unsigned int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&map, t]
        {
            const int base = static_cast<int>(t) * kPerThread;
            for (int i = 0; i < kPerThread; ++i)
                map.Insert(base + i, (base + i) * 2);
        });
    }
    for (std::thread& thread : threads) thread.join();

    PLU_CHECK_EQ(map.Size(), static_cast<std::size_t>(threadCount) * kPerThread);
    for (unsigned int t = 0; t < threadCount; ++t)
    {
        const int base = static_cast<int>(t) * kPerThread;
        for (int i = 0; i < kPerThread; ++i)
        {
            int value = -1;
            PLU_CHECK(map.Find(base + i, value));
            PLU_CHECK_EQ(value, (base + i) * 2);
        }
    }
}

PLU_TEST(ConcurrentHashMap_Stress_ContendedKeysInsertedExactlyOnce)
{
    // Every thread races to insert the SAME keys. Insert() returning true is the "I created
    // it" answer, so the true-returns summed over all threads must equal the key count —
    // no key may be created twice, none may be lost.
    Plu::ConcurrentHashMap<int, int> map;
    const unsigned int threadCount = PluTest::StressThreadCount();
    constexpr int kKeyCount = 2000;

    std::atomic<int> creations{0};
    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (unsigned int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&]
        {
            int local = 0;
            for (int i = 0; i < kKeyCount; ++i)
                if (map.Insert(i, i)) ++local;
            creations.fetch_add(local, std::memory_order_relaxed);
        });
    }
    for (std::thread& thread : threads) thread.join();

    PLU_CHECK_EQ(creations.load(), kKeyCount);
    PLU_CHECK_EQ(map.Size(), static_cast<std::size_t>(kKeyCount));
}

PLU_TEST(ConcurrentHashMap_Stress_VisitOrInsertCountersAreExact)
{
    // The Profiler pattern: many threads accumulate into a shared per-key counter. Because
    // the callback runs under the stripe lock, the increments must not lose an update.
    Plu::ConcurrentHashMap<int, long long> map;
    const unsigned int threadCount = PluTest::StressThreadCount();
    constexpr int kKeyCount = 64;
    constexpr int kBumpsPerKey = 2000;

    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (unsigned int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&map]
        {
            for (int bump = 0; bump < kBumpsPerKey; ++bump)
                for (int key = 0; key < kKeyCount; ++key)
                    map.VisitOrInsert(key, [](long long& counter) { ++counter; }, 0);
        });
    }
    for (std::thread& thread : threads) thread.join();

    PLU_CHECK_EQ(map.Size(), static_cast<std::size_t>(kKeyCount));
    const long long expected = static_cast<long long>(threadCount) * kBumpsPerKey;
    for (int key = 0; key < kKeyCount; ++key)
    {
        long long counter = -1;
        PLU_CHECK(map.Find(key, counter));
        PLU_CHECK_EQ(counter, expected);
    }
}

PLU_TEST(ConcurrentHashMap_Stress_ReadersDuringGrowth)
{
    // Readers hammer the map while writers force repeated rehashes. A reader must never see
    // a key it once observed disappear (nothing is removed here), and never read a torn value.
    Plu::ConcurrentHashMap<int, int> map;
    constexpr int kCount = 20000;

    std::atomic<bool> writersDone{false};
    std::atomic<int> tornReads{0};

    std::thread writer([&]
    {
        for (int i = 0; i < kCount; ++i) map.Insert(i, i * 7);
        writersDone.store(true, std::memory_order_release);
    });

    std::vector<std::thread> readers;
    const unsigned int readerCount = PluTest::StressThreadCount();
    readers.reserve(readerCount);
    for (unsigned int r = 0; r < readerCount; ++r)
    {
        readers.emplace_back([&]
        {
            while (!writersDone.load(std::memory_order_acquire))
            {
                for (int i = 0; i < kCount; i += 97)
                {
                    int value = -1;
                    if (map.Find(i, value) && value != i * 7)
                        tornReads.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    writer.join();
    for (std::thread& thread : readers) thread.join();

    PLU_CHECK_EQ(tornReads.load(), 0);
    PLU_CHECK_EQ(map.Size(), static_cast<std::size_t>(kCount));
}

PLU_TEST(ConcurrentHashMap_Stress_MixedInsertRemove)
{
    // Insert/Remove churn on overlapping keys. The invariant that survives the race is
    // Size() == the number of keys actually present, checked once everything has joined.
    Plu::ConcurrentHashMap<int, int> map;
    const unsigned int threadCount = PluTest::StressThreadCount();
    constexpr int kKeyCount = 1000;
    constexpr int kRounds = 200;

    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (unsigned int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&map, t]
        {
            for (int round = 0; round < kRounds; ++round)
            {
                for (int key = 0; key < kKeyCount; ++key)
                {
                    if ((key + static_cast<int>(t)) % 2 == 0) map.Insert(key, key);
                    else map.Remove(key);
                }
            }
        });
    }
    for (std::thread& thread : threads) thread.join();

    std::size_t counted = 0;
    map.ForEach([&counted](const int&, const int&) { ++counted; });
    PLU_CHECK_EQ(map.Size(), counted);
}
