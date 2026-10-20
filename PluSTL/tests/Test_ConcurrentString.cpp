//
// Created by Plutex on 2026-08-06.
//

#include "TestFramework.h"

#include "PluSTL/Concurrent/ConcurrentString.h"

#include <atomic>
#include <thread>
#include <vector>

// ============================================================================
// Single-threaded correctness
// ============================================================================

PLU_TEST(ConcurrentString_StartsEmpty)
{
    Plu::ConcurrentString text;
    PLU_CHECK(text.IsEmpty());
    PLU_CHECK_EQ(text.Length(), std::size_t{0});
    PLU_CHECK(text.Get() == Plu::String());
}

PLU_TEST(ConcurrentString_AssignAndAppend)
{
    Plu::ConcurrentString text("hello");
    PLU_CHECK(text.Get() == Plu::String("hello"));
    PLU_CHECK_EQ(text.Length(), std::size_t{5});

    text.Append(" world");
    PLU_CHECK(text.Get() == Plu::String("hello world"));

    text.Assign(Plu::String("replaced"));
    PLU_CHECK(text.Get() == Plu::String("replaced"));

    text.Clear();
    PLU_CHECK(text.IsEmpty());
}

PLU_TEST(ConcurrentString_Queries)
{
    Plu::ConcurrentString text("PluEngine renderer");

    PLU_CHECK(text.Contains("Engine"));
    PLU_CHECK_FALSE(text.Contains("Vulkan"));
    PLU_CHECK(text.StartsWith("Plu"));
    PLU_CHECK_FALSE(text.StartsWith("plu"));
    PLU_CHECK(text.EndsWith("renderer"));
    PLU_CHECK(text.Equals(Plu::String("PluEngine renderer")));
}

PLU_TEST(ConcurrentString_Replace)
{
    // Forwards to String::Replace, which replaces the FIRST occurrence only — the wrapper
    // deliberately does not invent different semantics from the type it guards.
    Plu::ConcurrentString text("a-b-c");
    text.Replace("-", "+");
    PLU_CHECK(text.Get() == Plu::String("a+b-c"));
}

PLU_TEST(ConcurrentString_ReserveKeepsContent)
{
    Plu::ConcurrentString text("short");
    text.Reserve(1024);
    PLU_CHECK(text.Get() == Plu::String("short"));
    // A second, smaller reserve must not shrink or corrupt anything.
    text.Reserve(8);
    PLU_CHECK(text.Get() == Plu::String("short"));
}

PLU_TEST(ConcurrentString_TakeEmptiesInOneStep)
{
    Plu::ConcurrentString text("accumulated log");
    const Plu::String taken = text.Take();

    PLU_CHECK(taken == Plu::String("accumulated log"));
    PLU_CHECK(text.IsEmpty());
    PLU_CHECK_EQ(text.Length(), std::size_t{0});

    // Taking again gives an empty string, not the old content.
    PLU_CHECK(text.Take().IsEmpty());
}

PLU_TEST(ConcurrentString_ReadAndWriteVisitors)
{
    Plu::ConcurrentString text("abc");

    std::size_t observedLength = 0;
    text.Read([&](const Plu::String& value) { observedLength = value.Length(); });
    PLU_CHECK_EQ(observedLength, std::size_t{3});

    // The read-modify-write escape hatch: append and conditionally flush, atomically.
    Plu::String flushed;
    text.Write([&](Plu::String& value)
    {
        value.Append("defgh");
        if (value.Length() >= 8)
        {
            flushed = value;
            value.Clear();
        }
    });

    PLU_CHECK(flushed == Plu::String("abcdefgh"));
    PLU_CHECK(text.IsEmpty());
}

// ============================================================================
// The String-shaped part of the API — same names, results by value
// ============================================================================

PLU_TEST(ConcurrentString_SearchesAndSlices)
{
    Plu::ConcurrentString text("a-b-c");

    PLU_CHECK_EQ(text.Find('-'), std::size_t{1});
    PLU_CHECK_EQ(text.Find('-', 2), std::size_t{3});
    PLU_CHECK_EQ(text.Find("b-"), std::size_t{2});
    PLU_CHECK_EQ(text.Find('z'), Plu::ConcurrentString::Npos);
    PLU_CHECK_EQ(text.RFind('-'), std::size_t{3});

    PLU_CHECK(text.Substring(2, 3) == Plu::String("b-c"));

    const DynamicArray<Plu::String> parts = text.Split('-');
    PLU_CHECK_EQ(parts.Size(), std::size_t{3});
    if (parts.Size() == 3)
    {
        PLU_CHECK(parts[0] == Plu::String("a"));
        PLU_CHECK(parts[2] == Plu::String("c"));
    }

    PLU_CHECK(text.ToUpper() == Plu::String("A-B-C"));
    PLU_CHECK(text.Get() == Plu::String("a-b-c")); // ToUpper copies, it does not mutate
    PLU_CHECK(text.Capacity() >= text.Length());
}

PLU_TEST(ConcurrentString_InPlaceEdits)
{
    Plu::ConcurrentString text("hello world");

    text.Insert(5, ",");
    PLU_CHECK(text.Get() == Plu::String("hello, world"));

    text.Remove(5, 1);
    PLU_CHECK(text.Get() == Plu::String("hello world"));

    text.ReplaceAt(0, 'H');
    PLU_CHECK(text.Get() == Plu::String("Hello world"));

    text.ToUpperInPlace();
    PLU_CHECK(text.Get() == Plu::String("HELLO WORLD"));
    text.ToLowerInPlace();
    PLU_CHECK(text.Get() == Plu::String("hello world"));
}

PLU_TEST(ConcurrentString_Operators)
{
    Plu::ConcurrentString text;

    text = "log:";
    text += Plu::String(" line1");
    text += " line2";

    PLU_CHECK(text.Get() == Plu::String("log: line1 line2"));
    PLU_CHECK(text == Plu::String("log: line1 line2"));
    PLU_CHECK(text != Plu::String("something else"));
    PLU_CHECK_EQ(text.Compare(Plu::String("log: line1 line2")), 0);
    PLU_CHECK(text.Equals("log: line1 line2"));
}

// ============================================================================
// Stress
// ============================================================================

PLU_TEST(ConcurrentString_Stress_AppendersAndReaders)
{
    // The log-accumulator shape: many threads append fixed-size chunks while readers copy
    // the buffer out. No append may be lost, and no reader may observe a torn length.
    Plu::ConcurrentString text;
    const unsigned int appenderCount = PluTest::StressThreadCount();
    constexpr int kAppendsPerThread = 2000;
    const char* kChunk = "0123456789"; // 10 chars
    constexpr std::size_t kChunkLength = 10;

    std::atomic<unsigned int> appendersDone{0};
    std::atomic<int> misalignedLengths{0};

    std::vector<std::thread> appenders;
    appenders.reserve(appenderCount);
    for (unsigned int a = 0; a < appenderCount; ++a)
    {
        appenders.emplace_back([&]
        {
            for (int i = 0; i < kAppendsPerThread; ++i) text.Append(kChunk);
            appendersDone.fetch_add(1, std::memory_order_release);
        });
    }

    std::thread reader([&]
    {
        while (appendersDone.load(std::memory_order_acquire) < appenderCount)
        {
            // Every observable state is a whole number of chunks — an append is atomic
            // with respect to any reader.
            const Plu::String copy = text.Get();
            if (copy.Length() % kChunkLength != 0)
                misalignedLengths.fetch_add(1, std::memory_order_relaxed);
        }
    });

    for (std::thread& thread : appenders) thread.join();
    reader.join();

    PLU_CHECK_EQ(misalignedLengths.load(), 0);
    PLU_CHECK_EQ(text.Length(),
                 static_cast<std::size_t>(appenderCount) * kAppendsPerThread * kChunkLength);
}

PLU_TEST(ConcurrentString_Stress_AppendAndTakeLoseNothing)
{
    // Producers append, one consumer repeatedly Take()s. The total number of characters
    // taken plus whatever is left must equal everything appended — Take() is the flush
    // primitive, and a flush that raced an append would show up as a shortfall here.
    Plu::ConcurrentString text;
    const unsigned int appenderCount = PluTest::StressThreadCount();
    constexpr int kAppendsPerThread = 3000;
    constexpr std::size_t kChunkLength = 4;

    std::atomic<unsigned int> appendersDone{0};
    std::atomic<std::size_t> takenTotal{0};

    std::vector<std::thread> appenders;
    appenders.reserve(appenderCount);
    for (unsigned int a = 0; a < appenderCount; ++a)
    {
        appenders.emplace_back([&]
        {
            for (int i = 0; i < kAppendsPerThread; ++i) text.Append("plu\n");
            appendersDone.fetch_add(1, std::memory_order_release);
        });
    }

    std::thread consumer([&]
    {
        while (appendersDone.load(std::memory_order_acquire) < appenderCount)
            takenTotal.fetch_add(text.Take().Length(), std::memory_order_relaxed);
        takenTotal.fetch_add(text.Take().Length(), std::memory_order_relaxed);
    });

    for (std::thread& thread : appenders) thread.join();
    consumer.join();

    const std::size_t expected =
        static_cast<std::size_t>(appenderCount) * kAppendsPerThread * kChunkLength;
    PLU_CHECK_EQ(takenTotal.load() + text.Length(), expected);
}

PLU_TEST(ConcurrentString_Stress_WriteVisitorSerializesReadModifyWrite)
{
    // Write() must be exclusive: a counter maintained by read-modify-write inside the
    // visitor cannot lose an update.
    Plu::ConcurrentString text("");
    const unsigned int threadCount = PluTest::StressThreadCount();
    constexpr int kBumpsPerThread = 5000;

    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (unsigned int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&]
        {
            for (int i = 0; i < kBumpsPerThread; ++i)
                text.Write([](Plu::String& value) { value.Append("."); });
        });
    }
    for (std::thread& thread : threads) thread.join();

    PLU_CHECK_EQ(text.Length(), static_cast<std::size_t>(threadCount) * kBumpsPerThread);
}
