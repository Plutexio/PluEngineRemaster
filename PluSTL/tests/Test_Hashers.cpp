//
// Created by Plutex on 2026-09-18.
//
// Hashers and allocators. These are the pieces every container sits on, and the
// failures they had were silent: a hash that collapsed to one bucket still returned
// correct answers, and an allocator that ignored alignment still "worked" on the
// types that happened not to be over-aligned.

#include "TestFramework.h"

#include "PluSTL/Allocators/Default.h"
#include "PluSTL/Array/Array.h"
#include "PluSTL/HashMap/HashMap.h"
#include "PluSTL/Hashers/Default.h"
#include "PluSTL/Hashers/String.h"
#include "PluSTL/Path/Path.h"
#include "PluSTL/String/String.h"

#include <cstdint>
#include <set>
#include <utility>

namespace
{
    enum class Colour : std::uint8_t { Red, Green, Blue };

    struct PaddedKey
    {
        std::uint32_t Index = 0;
        std::uint32_t Generation = 0;
        bool Failed = true;

        bool operator==(const PaddedKey& other) const
        {
            return Index == other.Index && Generation == other.Generation && Failed == other.Failed;
        }
    };

    // Over-aligned on purpose: DefaultAllocator used to hand this back off a plain
    // ::operator new, which only guarantees __STDCPP_DEFAULT_NEW_ALIGNMENT__.
    struct alignas(64) CacheLineAligned
    {
        int Value = 0;
    };
}

namespace Plu
{
    // The same shape as the engine's DefaultHash<EngineObjectHandle>: a struct with
    // padding needs a hand-written hasher, and it must not collapse.
    template<> struct DefaultHash<PaddedKey>
    {
        std::size_t operator()(const PaddedKey& key) const noexcept
        {
            std::size_t hash = DefaultHash<std::uint32_t>{}(key.Index);
            HashCombine(hash, DefaultHash<std::uint32_t>{}(key.Generation));
            HashCombine(hash, key.Failed ? 0x9E3779B97F4A7C15ULL : 0ULL);
            return hash;
        }
    };
}

// ============================================================================
// Integers and enums
// ============================================================================

PLU_TEST(Hashers_IntegersOfEveryWidthAreDistinctAndSpread)
{
    // Small integer types had no specialization and fell through to a byte-wise FNV.
    // Two properties matter, and neither is "how many distinct low-6-bit values do 64
    // keys produce" — a perfect random hash only manages ~40 of 64 there, so that
    // number measures the birthday paradox, not the hasher.
    //
    //   1. sequential keys must not collide with each other at all, and
    //   2. they must not pile into one bucket — the failure the handle hasher had.
    auto quality = [](auto sample, int count) {
        using T = decltype(sample);
        std::set<std::size_t> distinct;
        int buckets[64] = {};
        for (int i = 0; i < count; ++i)
        {
            const std::size_t hash = Plu::DefaultHash<T>{}(static_cast<T>(i));
            distinct.insert(hash);
            ++buckets[hash & 63];
        }
        int busiest = 0;
        for (int load : buckets) busiest = load > busiest ? load : busiest;
        return std::pair<std::size_t, int>{distinct.size(), busiest};
    };

    auto check = [&](auto sample, int count) {
        const auto [distinct, busiest] = quality(sample, count);
        PLU_CHECK_EQ(distinct, static_cast<std::size_t>(count)); // no self-collisions
        PLU_CHECK(busiest <= count / 16);                        // 4x the even share
    };

    check(std::uint8_t{}, 256);   // the whole domain
    check(char{}, 128);
    check(std::uint16_t{}, 1024);
    check(std::int16_t{}, 1024);
    check(std::uint32_t{}, 1024);
    check(std::int32_t{}, 1024);
    check(std::uint64_t{}, 1024);
    check(std::int64_t{}, 1024);
}

PLU_TEST(Hashers_EnumsHashByUnderlyingValue)
{
    const std::size_t red = Plu::DefaultHash<Colour>{}(Colour::Red);
    const std::size_t green = Plu::DefaultHash<Colour>{}(Colour::Green);
    PLU_CHECK(red != green);
    PLU_CHECK_EQ(green, Plu::DefaultHash<std::uint8_t>{}(1));

    Plu::HashMap<Colour, int> map;
    map.Insert(Colour::Red, 1);
    map.Insert(Colour::Blue, 3);
    PLU_CHECK_EQ(*map.Find(Colour::Red), 1);
    PLU_CHECK(map.Find(Colour::Green) == nullptr);
}

PLU_TEST(Hashers_NoIntegerHashCollapsesToZero)
{
    // DefaultHash<EngineObjectHandle> multiplied by the hash of 0, and the MurmurHash3
    // finalizer of 0 is 0 — so every valid handle hashed to 0.
    int zeros = 0;
    for (int i = 0; i < 1000; ++i)
        if (Plu::DefaultHash<int>{}(i) == 0) ++zeros;
    PLU_CHECK(zeros <= 1);
}

// ============================================================================
// Floating point
// ============================================================================

PLU_TEST(Hashers_FloatCanonicalizesNegativeZero)
{
    // -0.0f == 0.0f, so they must hash the same or a map can hold two "equal" keys.
    PLU_CHECK_EQ(Plu::DefaultHash<float>{}(0.0f), Plu::DefaultHash<float>{}(-0.0f));
    PLU_CHECK_EQ(Plu::DefaultHash<double>{}(0.0), Plu::DefaultHash<double>{}(-0.0));

    Plu::HashMap<float, int> map;
    map.Insert(0.0f, 1);
    PLU_CHECK_FALSE(map.Insert(-0.0f, 2)); // same key, rejected as a duplicate
    PLU_CHECK_EQ(map.Size(), std::size_t{1});
}

PLU_TEST(Hashers_FloatKeysAreDistinct)
{
    std::set<std::size_t> hashes;
    for (int i = 1; i <= 100; ++i) hashes.insert(Plu::DefaultHash<float>{}(static_cast<float>(i) * 0.5f));
    PLU_CHECK_EQ(hashes.size(), std::size_t{100});
}

// ============================================================================
// Structs with padding
// ============================================================================

PLU_TEST(Hashers_HashCombineNeverCollapses)
{
    // HashCombine has to survive a zero on either side; a plain multiply does not.
    std::size_t withZero = 0;
    Plu::HashCombine(withZero, 0);
    PLU_CHECK(withZero != 0);

    std::size_t seeded = 12345;
    Plu::HashCombine(seeded, 0);
    PLU_CHECK(seeded != 12345);
    PLU_CHECK(seeded != 0);
}

PLU_TEST(Hashers_PaddedStructKeySpreadsAcrossBuckets)
{
    std::set<std::size_t> hashes;
    for (std::uint32_t i = 0; i < 200; ++i)
        hashes.insert(Plu::DefaultHash<PaddedKey>{}(PaddedKey{i, 1, false}));
    PLU_CHECK_EQ(hashes.size(), std::size_t{200});

    // The bug that mattered: every "valid" key (Failed == false) hashing to 0.
    int zeros = 0;
    for (std::uint32_t i = 0; i < 200; ++i)
        if (Plu::DefaultHash<PaddedKey>{}(PaddedKey{i, i, false}) == 0) ++zeros;
    PLU_CHECK_EQ(zeros, 0);

    Plu::HashMap<PaddedKey, int> map;
    for (std::uint32_t i = 0; i < 200; ++i) map.Insert(PaddedKey{i, 1, false}, static_cast<int>(i));
    PLU_CHECK_EQ(map.Size(), std::size_t{200});
    PLU_CHECK_EQ(*map.Find(PaddedKey{57, 1, false}), 57);
    PLU_CHECK(map.Find(PaddedKey{57, 2, false}) == nullptr);
}

// ============================================================================
// Strings, paths and pointers
// ============================================================================

PLU_TEST(Hashers_EqualStringsHashEqually)
{
    Plu::String a("the-same-text");
    Plu::String b("the-same-text");
    PLU_CHECK_EQ(Plu::DefaultHash<Plu::String>{}(a), Plu::DefaultHash<Plu::String>{}(b));

    Plu::String c("the-same-texu");
    PLU_CHECK(Plu::DefaultHash<Plu::String>{}(a) != Plu::DefaultHash<Plu::String>{}(c));

    // Crossing the SSO boundary must not change the rule.
    Plu::String longA("a-string-comfortably-past-the-small-string-optimization-buffer");
    Plu::String longB("a-string-comfortably-past-the-small-string-optimization-buffer");
    PLU_CHECK_EQ(Plu::DefaultHash<Plu::String>{}(longA), Plu::DefaultHash<Plu::String>{}(longB));
}

PLU_TEST(Hashers_EqualPathsHashEqually)
{
    Plu::Path a("/home/plutex/assets/mesh.fbx");
    Plu::Path b("/home/plutex/assets/mesh.fbx");
    PLU_CHECK_EQ(Plu::DefaultHash<Plu::Path>{}(a), Plu::DefaultHash<Plu::Path>{}(b));

    Plu::Path other("/home/plutex/assets/other.fbx");
    PLU_CHECK(Plu::DefaultHash<Plu::Path>{}(a) != Plu::DefaultHash<Plu::Path>{}(other));

    Plu::HashMap<Plu::Path, int> map;
    map.Insert(a, 1);
    PLU_CHECK_EQ(*map.Find(Plu::Path("/home/plutex/assets/mesh.fbx")), 1);
}

PLU_TEST(Hashers_CStringsHashByContentBothConstnesses)
{
    char buffer[] = "text";
    const char* asConst = "text";
    char* asMutable = buffer;

    // Without a char* specialization this would go through DefaultHash<T*> and hash
    // two different addresses instead of the identical text.
    PLU_CHECK_EQ(Plu::DefaultHash<const char*>{}(asConst), Plu::DefaultHash<char*>{}(asMutable));
    PLU_CHECK_EQ(Plu::DefaultHash<const char*>{}(nullptr), std::size_t{0});
}

PLU_TEST(Hashers_PointersHashByAddress)
{
    int a = 1;
    int b = 2;
    PLU_CHECK(Plu::DefaultHash<int*>{}(&a) != Plu::DefaultHash<int*>{}(&b));
    PLU_CHECK_EQ(Plu::DefaultHash<int*>{}(&a), Plu::DefaultHash<int*>{}(&a));

    Plu::HashMap<void*, int> map;
    map.Insert(&a, 1);
    map.Insert(&b, 2);
    PLU_CHECK_EQ(*map.Find(&a), 1);
}

// ============================================================================
// Allocators
// ============================================================================

PLU_TEST(Allocator_RespectsOverAlignment)
{
    Plu::DefaultAllocator<CacheLineAligned> allocator;
    CacheLineAligned* block = allocator.Allocate(8);
    PLU_CHECK(block != nullptr);
    PLU_CHECK_EQ(reinterpret_cast<std::uintptr_t>(block) % 64, std::uintptr_t{0});
    allocator.Deallocate(block, 8);

    // The same through a container, which is where it actually bites.
    Plu::DynamicArray<CacheLineAligned> array;
    for (int i = 0; i < 100; ++i) array.PushBack(CacheLineAligned{i});
    PLU_CHECK_EQ(reinterpret_cast<std::uintptr_t>(array.Data()) % 64, std::uintptr_t{0});
    PLU_CHECK_EQ(array[99].Value, 99);
}

PLU_TEST(Allocator_RejectsOverflowingRequest)
{
    // count * sizeof(T) used to wrap silently into a small allocation.
    Plu::DefaultAllocator<std::uint64_t> allocator;
    PLU_CHECK(allocator.Allocate(SIZE_MAX) == nullptr);
    PLU_CHECK(allocator.Allocate(SIZE_MAX / 4) == nullptr);
    PLU_CHECK(allocator.Allocate(0) == nullptr);
}

PLU_TEST(Allocator_RebindTargetsTheRightType)
{
    // A container that needs storage for something other than T (nodes, slots) must
    // get there through the caller's allocator, not a hardcoded DefaultAllocator.
    static_assert(std::is_same_v<Plu::RebindAllocatorT<Plu::DefaultAllocator<int>, double>,
                                 Plu::DefaultAllocator<double>>);

    // An allocator with no Rebind alias still resolves, to the previous behaviour.
    struct NoRebind { using ValueType = int; };
    static_assert(std::is_same_v<Plu::RebindAllocatorT<NoRebind, char>, Plu::DefaultAllocator<char>>);

    PLU_CHECK(true); // the assertions above are the test
}
