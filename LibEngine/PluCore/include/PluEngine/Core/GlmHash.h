//
// Created by Plutex on 2026-09-19.
//

#ifndef PLUENGINE_GLMHASH_H
#define PLUENGINE_GLMHASH_H

#include <cstddef>

#include "glm/fwd.hpp"
#include "glm/detail/type_quat.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

#include "PluSTL/Hashers/Default.h"

// ============================================================================
// DefaultHash for glm types — the bridge that lets Vec3 & friends key a HashMap
// ============================================================================
//
// Without these, a glm vector falls through to DefaultHash's byte-wise fallback.
// It compiles (these types are trivially copyable and have no padding) and it is
// wrong in one specific way: it hashes the float *bits*, while operator== compares
// the float *values*. The two disagree on negative zero — `-0.0f == 0.0f` is true
// but their bit patterns differ — so a container can end up holding two entries
// under keys that compare equal. `PhysicsWorld` keys a scaled-shape cache by
// `GetWorldScale()`, which is scene data and can carry a -0.0 component.
//
// Going component by component routes every float through DefaultHash<float>,
// which canonicalizes -0.0 to +0.0 (and every NaN to one value) before mixing.
//
// Note what this still cannot do: a NaN component makes operator== false against
// *itself*, so a NaN key never matches on lookup however it is hashed. That is an
// equality property, not a hashing one. Keep NaN out of keys.
//
// Included by PluTypes.h, so any translation unit that can name Vec3 can also key
// a HashMap with it — the hasher travels with the type, the same rule String.h and
// Path.h follow for BasicString / BasicPath.

namespace Plu
{
    template<glm::length_t L, typename T, glm::qualifier Q>
    struct DefaultHash<glm::vec<L, T, Q>>
    {
        std::size_t operator()(const glm::vec<L, T, Q>& value) const noexcept
        {
            std::size_t hash = DefaultHash<T>{}(value[0]);
            for (glm::length_t i = 1; i < L; ++i)
                HashCombine(hash, DefaultHash<T>{}(value[i]));
            return hash;
        }
    };

    template<typename T, glm::qualifier Q>
    struct DefaultHash<glm::qua<T, Q>>
    {
        std::size_t operator()(const glm::qua<T, Q>& value) const noexcept
        {
            std::size_t hash = DefaultHash<T>{}(value[0]);
            for (glm::length_t i = 1; i < 4; ++i)
                HashCombine(hash, DefaultHash<T>{}(value[i]));
            return hash;
        }
    };

    // Column by column, each column through the vector hasher above.
    template<glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
    struct DefaultHash<glm::mat<C, R, T, Q>>
    {
        std::size_t operator()(const glm::mat<C, R, T, Q>& value) const noexcept
        {
            using Column = glm::vec<R, T, Q>;
            std::size_t hash = DefaultHash<Column>{}(value[0]);
            for (glm::length_t i = 1; i < C; ++i)
                HashCombine(hash, DefaultHash<Column>{}(value[i]));
            return hash;
        }
    };
}

#endif //PLUENGINE_GLMHASH_H
