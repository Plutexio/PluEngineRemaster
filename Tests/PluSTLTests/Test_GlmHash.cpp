//
// Created by Plutex on 2026-09-19.
//
// DefaultHash for the glm types (PluBase/GlmHash.h). That header depends on
// glm and PluSTL only, so the container suite can cover it without building
// PluBase or the engine.

#include "TestFramework.h"

#include "PluBase/GlmHash.h"

#include "PluSTL/HashMap/HashMap.h"
#include "PluSTL/HashSet/HashSet.h"

#include <cmath>
#include <set>

using Vec3 = glm::vec3;
using Vec4 = glm::vec4;

PLU_TEST(GlmHash_NegativeZeroHashesAsPositiveZero)
{
    // The whole point. -0.0f == 0.0f, so a byte-wise hash of the vector lets two
    // keys that compare equal land in different buckets — and a cache keyed by a
    // scale vector (PhysicsWorld's scaled-shape cache) then holds both.
    const Vec3 positive(0.0f, 1.0f, 2.0f);
    const Vec3 negative(-0.0f, 1.0f, 2.0f);

    PLU_CHECK(positive == negative);                      // glm agrees they are equal
    PLU_CHECK_EQ(Plu::DefaultHash<Vec3>{}(positive), Plu::DefaultHash<Vec3>{}(negative));

    Plu::HashMap<Vec3, int> cache;
    cache.Insert(positive, 1);
    PLU_CHECK_FALSE(cache.Insert(negative, 2)); // recognized as the same key
    PLU_CHECK_EQ(cache.Size(), std::size_t{1});
    PLU_CHECK_EQ(*cache.Find(negative), 1);

    // Every component position, and the all-zero vector both ways.
    PLU_CHECK_EQ(Plu::DefaultHash<Vec3>{}(Vec3(1.0f, -0.0f, 2.0f)),
                 Plu::DefaultHash<Vec3>{}(Vec3(1.0f, 0.0f, 2.0f)));
    PLU_CHECK_EQ(Plu::DefaultHash<Vec3>{}(Vec3(1.0f, 2.0f, -0.0f)),
                 Plu::DefaultHash<Vec3>{}(Vec3(1.0f, 2.0f, 0.0f)));
    PLU_CHECK_EQ(Plu::DefaultHash<Vec3>{}(Vec3(-0.0f)), Plu::DefaultHash<Vec3>{}(Vec3(0.0f)));
}

PLU_TEST(GlmHash_ComponentOrderMatters)
{
    // A hasher that just added or xor'd the components would call these equal.
    PLU_CHECK(Plu::DefaultHash<Vec3>{}(Vec3(1.0f, 2.0f, 3.0f))
           != Plu::DefaultHash<Vec3>{}(Vec3(3.0f, 2.0f, 1.0f)));
    PLU_CHECK(Plu::DefaultHash<Vec3>{}(Vec3(1.0f, 0.0f, 0.0f))
           != Plu::DefaultHash<Vec3>{}(Vec3(0.0f, 1.0f, 0.0f)));
}

PLU_TEST(GlmHash_EqualVectorsHashEqually)
{
    const Vec3 a(1.5f, -2.25f, 1024.0f);
    const Vec3 b(1.5f, -2.25f, 1024.0f);
    PLU_CHECK_EQ(Plu::DefaultHash<Vec3>{}(a), Plu::DefaultHash<Vec3>{}(b));

    // A one-ulp difference is a different key, as it should be.
    Vec3 nudged = a;
    nudged.x = std::nextafter(a.x, 2.0f);
    PLU_CHECK(Plu::DefaultHash<Vec3>{}(a) != Plu::DefaultHash<Vec3>{}(nudged));
}

PLU_TEST(GlmHash_SpreadsOverBuckets)
{
    // Half-extent vectors the way StaticMeshBoundingBoxCollisionData builds them:
    // small, positive, densely spaced. These must not pile into one bucket.
    std::set<std::size_t> distinct;
    int buckets[64] = {};
    for (int i = 1; i <= 500; ++i)
    {
        const Vec3 extent(static_cast<float>(i) * 0.01f,
                          static_cast<float>(i) * 0.02f,
                          static_cast<float>(i) * 0.005f);
        const std::size_t hash = Plu::DefaultHash<Vec3>{}(extent);
        distinct.insert(hash);
        ++buckets[hash & 63];
    }
    int busiest = 0;
    for (int load : buckets) busiest = load > busiest ? load : busiest;

    PLU_CHECK_EQ(distinct.size(), std::size_t{500});
    PLU_CHECK(busiest <= 500 / 16);
}

PLU_TEST(GlmHash_IntegerVectorsAndOtherLengths)
{
    using IVec3 = glm::ivec3;
    Plu::HashMap<IVec3, int> grid;
    for (int x = 0; x < 10; ++x)
        for (int y = 0; y < 10; ++y)
            PLU_CHECK(grid.Insert(IVec3(x, y, 0), x * 10 + y));

    PLU_CHECK_EQ(grid.Size(), std::size_t{100});
    PLU_CHECK_EQ(*grid.Find(IVec3(3, 7, 0)), 37);
    PLU_CHECK(grid.Find(IVec3(3, 7, 1)) == nullptr);

    // vec2 and vec4 go through the same partial specialization.
    PLU_CHECK_EQ(Plu::DefaultHash<glm::vec2>{}(glm::vec2(-0.0f, 1.0f)),
                 Plu::DefaultHash<glm::vec2>{}(glm::vec2(0.0f, 1.0f)));
    PLU_CHECK_EQ(Plu::DefaultHash<Vec4>{}(Vec4(1.0f, 2.0f, 3.0f, -0.0f)),
                 Plu::DefaultHash<Vec4>{}(Vec4(1.0f, 2.0f, 3.0f, 0.0f)));

    Plu::HashSet<Vec3> seen;
    PLU_CHECK(seen.Insert(Vec3(1.0f)));
    PLU_CHECK_FALSE(seen.Insert(Vec3(1.0f)));
    PLU_CHECK(seen.Contains(Vec3(1.0f)));
}

PLU_TEST(GlmHash_QuaternionsAndMatrices)
{
    using Quaternion = glm::quat;
    using Matrix4 = glm::mat4;

    const Quaternion identity(1.0f, 0.0f, 0.0f, 0.0f);
    const Quaternion negativeZero(1.0f, -0.0f, 0.0f, 0.0f);
    PLU_CHECK_EQ(Plu::DefaultHash<Quaternion>{}(identity), Plu::DefaultHash<Quaternion>{}(negativeZero));
    PLU_CHECK(Plu::DefaultHash<Quaternion>{}(identity)
           != Plu::DefaultHash<Quaternion>{}(Quaternion(0.0f, 1.0f, 0.0f, 0.0f)));

    Matrix4 a(1.0f);
    Matrix4 b(1.0f);
    PLU_CHECK_EQ(Plu::DefaultHash<Matrix4>{}(a), Plu::DefaultHash<Matrix4>{}(b));
    b[3][0] = 5.0f;
    PLU_CHECK(Plu::DefaultHash<Matrix4>{}(a) != Plu::DefaultHash<Matrix4>{}(b));
}
