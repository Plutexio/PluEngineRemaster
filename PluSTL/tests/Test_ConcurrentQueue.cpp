//
// Created by Plutex on 2026-08-06.
//

#include "TestFramework.h"

#include "PluSTL/Concurrent/ConcurrentQueue.h"
#include "PluSTL/String/String.h"

#include <atomic>
#include <thread>
#include <vector>

// ============================================================================
// ConcurrentQueue — single-threaded correctness
// ============================================================================

PLU_TEST(ConcurrentQueue_StartsEmpty)
{
    Plu::ConcurrentQueue<int> queue;
    PLU_CHECK(queue.IsEmpty());
    PLU_CHECK_EQ(queue.Size(), std::size_t{0});

    Plu::Queue<int> drained;
    queue.Drain(drained);
    PLU_CHECK_EQ(drained.Size(), std::size_t{0});
}

PLU_TEST(ConcurrentQueue_DrainReturnsPushOrder)
{
    Plu::ConcurrentQueue<int> queue;
    for (int i = 0; i < 10; ++i) queue.PushBack(i);
    PLU_CHECK_EQ(queue.Size(), std::size_t{10});

    Plu::Queue<int> drained;
    queue.Drain(drained);

    PLU_CHECK_EQ(drained.Size(), std::size_t{10});
    for (std::size_t i = 0; i < drained.Size(); ++i)
        PLU_CHECK_EQ(drained[i], static_cast<int>(i));

    // The queue is empty afterwards, and draining again is harmless.
    PLU_CHECK(queue.IsEmpty());
    queue.Drain(drained);
    PLU_CHECK_EQ(drained.Size(), std::size_t{0});
}

PLU_TEST(ConcurrentQueue_DrainDiscardsWhateverTheScratchHeld)
{
    Plu::ConcurrentQueue<int> queue;
    queue.PushBack(1);

    Plu::Queue<int> scratch;
    scratch.PushBack(999); // leftovers from a previous frame
    queue.Drain(scratch);

    PLU_CHECK_EQ(scratch.Size(), std::size_t{1});
    PLU_CHECK_EQ(scratch[0], 1); // and the leftover must not have leaked into the queue
    PLU_CHECK(queue.IsEmpty());
}

PLU_TEST(ConcurrentQueue_PushBackUnique)
{
    Plu::ConcurrentQueue<int> queue;
    PLU_CHECK(queue.PushBackUnique(5));
    PLU_CHECK_FALSE(queue.PushBackUnique(5));
    PLU_CHECK(queue.PushBackUnique(6));
    PLU_CHECK_EQ(queue.Size(), std::size_t{2});

    // Dedupe covers the *pending* batch only — once drained, the same value may be
    // queued again. That is the intended semantics for "re-request until it lands".
    Plu::Queue<int> drained;
    queue.Drain(drained);
    PLU_CHECK(queue.PushBackUnique(5));
}

PLU_TEST(ConcurrentQueue_PushBackUniqueIf)
{
    Plu::ConcurrentQueue<int> queue;
    queue.PushBack(10);
    queue.PushBack(20);

    // "Same request" defined by the caller: anything in the same decade.
    PLU_CHECK_FALSE(queue.PushBackUniqueIf(11, [](const int& queued) { return queued / 10 == 1; }));
    PLU_CHECK(queue.PushBackUniqueIf(31, [](const int& queued) { return queued / 10 == 3; }));
    PLU_CHECK_EQ(queue.Size(), std::size_t{3});
}

PLU_TEST(ConcurrentQueue_MoveOnlyPayload)
{
    // The real queues carry String / Path / TUsePointer — everything must move, not copy.
    Plu::ConcurrentQueue<Plu::String> queue;
    queue.PushBack(Plu::String("first"));
    Plu::String second("second");
    queue.PushBack(static_cast<Plu::String&&>(second));
    queue.EmplaceBack("third");

    Plu::Queue<Plu::String> drained;
    queue.Drain(drained);

    PLU_CHECK_EQ(drained.Size(), std::size_t{3});
    PLU_CHECK(drained[0] == Plu::String("first"));
    PLU_CHECK(drained[1] == Plu::String("second"));
    PLU_CHECK(drained[2] == Plu::String("third"));
}

PLU_TEST(ConcurrentQueue_Clear)
{
    Plu::ConcurrentQueue<int> queue;
    for (int i = 0; i < 50; ++i) queue.PushBack(i);
    queue.Clear();
    PLU_CHECK(queue.IsEmpty());

    Plu::Queue<int> drained;
    queue.Drain(drained);
    PLU_CHECK_EQ(drained.Size(), std::size_t{0});
}

PLU_TEST(ConcurrentQueue_TryPopFrontIsFIFO)
{
    // The Queue half of the API: one item at a time, oldest first.
    Plu::ConcurrentQueue<int> queue;
    for (int i = 0; i < 4; ++i) queue.PushBack(i);

    int value = -1;
    for (int expected = 0; expected < 4; ++expected)
    {
        PLU_CHECK(queue.TryPopFront(value));
        PLU_CHECK_EQ(value, expected);
    }

    PLU_CHECK(queue.IsEmpty());
    value = 777;
    PLU_CHECK_FALSE(queue.TryPopFront(value));
    PLU_CHECK_EQ(value, 777); // untouched on an empty queue

    // Popping and draining share one buffer — what a pop took must not come back.
    queue.PushBack(10);
    queue.PushBack(20);
    PLU_CHECK(queue.TryPopFront(value));
    PLU_CHECK_EQ(value, 10);

    Plu::Queue<int> drained;
    queue.Drain(drained);
    PLU_CHECK_EQ(drained.Size(), std::size_t{1});
    PLU_CHECK_EQ(drained.Front(), 20);
}

// ============================================================================
// ConcurrentQueue — stress (MPSC, the RenderingManager shape)
// ============================================================================

PLU_TEST(ConcurrentQueue_Stress_ManyProducersOneDrainer)
{
    // Every pushed value must be drained exactly once — nothing lost, nothing duplicated.
    Plu::ConcurrentQueue<int> queue;
    const unsigned int producerCount = PluTest::StressThreadCount();
    constexpr int kPerProducer = 20000;

    std::atomic<unsigned int> producersDone{0};
    std::vector<int> seenCount(static_cast<std::size_t>(producerCount) * kPerProducer, 0);

    std::vector<std::thread> producers;
    producers.reserve(producerCount);
    for (unsigned int p = 0; p < producerCount; ++p)
    {
        producers.emplace_back([&, p]
        {
            const int base = static_cast<int>(p) * kPerProducer;
            for (int i = 0; i < kPerProducer; ++i) queue.PushBack(base + i);
            producersDone.fetch_add(1, std::memory_order_release);
        });
    }

    std::thread consumer([&]
    {
        Plu::Queue<int> scratch; // a local, exactly as the docs require
        while (producersDone.load(std::memory_order_acquire) < producerCount)
        {
            queue.Drain(scratch);
            for (int value : scratch) ++seenCount[static_cast<std::size_t>(value)];
        }
        queue.Drain(scratch);
        for (int value : scratch) ++seenCount[static_cast<std::size_t>(value)];
    });

    for (std::thread& thread : producers) thread.join();
    consumer.join();

    PLU_CHECK(queue.IsEmpty());
    bool allSeenOnce = true;
    for (int count : seenCount) if (count != 1) allSeenOnce = false;
    PLU_CHECK(allSeenOnce);
}

PLU_TEST(ConcurrentQueue_Stress_PushBackUniqueNeverDuplicatesWithinABatch)
{
    // Several threads re-request the same handful of items every iteration while one
    // consumer drains. No drained batch may ever contain a value twice.
    Plu::ConcurrentQueue<int> queue;
    const unsigned int producerCount = PluTest::StressThreadCount();
    constexpr int kDistinctValues = 16;
    constexpr int kRounds = 5000;

    std::atomic<unsigned int> producersDone{0};
    std::atomic<int> duplicatesInABatch{0};

    std::vector<std::thread> producers;
    producers.reserve(producerCount);
    for (unsigned int p = 0; p < producerCount; ++p)
    {
        producers.emplace_back([&]
        {
            for (int round = 0; round < kRounds; ++round)
                for (int value = 0; value < kDistinctValues; ++value)
                    queue.PushBackUnique(value);
            producersDone.fetch_add(1, std::memory_order_release);
        });
    }

    std::thread consumer([&]
    {
        Plu::Queue<int> scratch;
        auto inspect = [&]
        {
            bool seen[kDistinctValues] = {};
            for (int value : scratch)
            {
                if (seen[value]) duplicatesInABatch.fetch_add(1, std::memory_order_relaxed);
                seen[value] = true;
            }
        };
        while (producersDone.load(std::memory_order_acquire) < producerCount)
        {
            queue.Drain(scratch);
            inspect();
        }
        queue.Drain(scratch);
        inspect();
    });

    for (std::thread& thread : producers) thread.join();
    consumer.join();

    PLU_CHECK_EQ(duplicatesInABatch.load(), 0);
}

// ============================================================================
// ConcurrentRingQueue — bounded SPSC
// ============================================================================

PLU_TEST(ConcurrentRingQueue_PushPopSingleThreaded)
{
    Plu::ConcurrentRingQueue<int, 8> ring;
    PLU_CHECK(ring.IsEmpty());

    int value = -1;
    PLU_CHECK_FALSE(ring.TryPopFront(value));
    PLU_CHECK_EQ(value, -1);

    PLU_CHECK(ring.TryPushBack(1));
    PLU_CHECK(ring.TryPushBack(2));
    PLU_CHECK_EQ(ring.Size(), std::size_t{2});

    PLU_CHECK(ring.TryPopFront(value));
    PLU_CHECK_EQ(value, 1);
    PLU_CHECK(ring.TryPopFront(value));
    PLU_CHECK_EQ(value, 2);
    PLU_CHECK(ring.IsEmpty());
}

PLU_TEST(ConcurrentRingQueue_RejectsPushWhenFull)
{
    Plu::ConcurrentRingQueue<int, 8> ring;
    // One slot is the full/empty discriminator, so the usable depth is Capacity - 1.
    for (std::size_t i = 0; i < ring.kMaxDepth; ++i)
        PLU_CHECK(ring.TryPushBack(static_cast<int>(i)));

    PLU_CHECK_FALSE(ring.TryPushBack(999));
    PLU_CHECK_EQ(ring.Size(), ring.kMaxDepth);

    // Popping one makes room again — the indices really do wrap.
    int value = 0;
    PLU_CHECK(ring.TryPopFront(value));
    PLU_CHECK(ring.TryPushBack(999));
}

PLU_TEST(ConcurrentRingQueue_WrapsAroundManyTimes)
{
    Plu::ConcurrentRingQueue<int, 4> ring;
    for (int i = 0; i < 1000; ++i)
    {
        PLU_CHECK(ring.TryPushBack(i));
        int value = -1;
        PLU_CHECK(ring.TryPopFront(value));
        PLU_CHECK_EQ(value, i);
    }
}

PLU_TEST(ConcurrentRingQueue_Stress_SingleProducerSingleConsumer)
{
    // Bounded ring: the producer retries on a full ring, the consumer spins on an empty one.
    // Everything must arrive, in order, exactly once.
    Plu::ConcurrentRingQueue<int, 64> ring;
    constexpr int kCount = 200000;

    std::atomic<bool> producerDone{false};
    std::atomic<int> outOfOrder{0};
    std::atomic<int> received{0};

    std::thread producer([&]
    {
        for (int i = 0; i < kCount; ++i)
            while (!ring.TryPushBack(i)) std::this_thread::yield();
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&]
    {
        int expected = 0;
        int value = 0;
        while (expected < kCount)
        {
            if (ring.TryPopFront(value))
            {
                if (value != expected) outOfOrder.fetch_add(1, std::memory_order_relaxed);
                ++expected;
                received.fetch_add(1, std::memory_order_relaxed);
            }
            else if (producerDone.load(std::memory_order_acquire) && ring.IsEmpty())
            {
                break;
            }
        }
    });

    producer.join();
    consumer.join();

    PLU_CHECK_EQ(received.load(), kCount);
    PLU_CHECK_EQ(outOfOrder.load(), 0);
    PLU_CHECK(ring.IsEmpty());
}

PLU_TEST(ConcurrentRingQueue_Stress_MovePayload)
{
    // Same, with a payload that owns a heap buffer — catches a slot that is popped without
    // being released (the moved-from slot must be reset, or the ring leaks on wrap).
    Plu::ConcurrentRingQueue<Plu::String, 16> ring;
    constexpr int kCount = 20000;

    std::atomic<int> mismatches{0};

    std::thread producer([&]
    {
        for (int i = 0; i < kCount; ++i)
        {
            Plu::String payload = Plu::String("item_") + Plu::String::FromInt(i);
            while (!ring.TryPushBack(static_cast<Plu::String&&>(payload))) std::this_thread::yield();
        }
    });

    std::thread consumer([&]
    {
        Plu::String value;
        for (int i = 0; i < kCount; ++i)
        {
            while (!ring.TryPopFront(value)) std::this_thread::yield();
            if (value != Plu::String("item_") + Plu::String::FromInt(i))
                mismatches.fetch_add(1, std::memory_order_relaxed);
        }
    });

    producer.join();
    consumer.join();

    PLU_CHECK_EQ(mismatches.load(), 0);
}
