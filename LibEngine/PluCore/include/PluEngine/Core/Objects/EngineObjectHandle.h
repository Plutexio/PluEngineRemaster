//
// Created by Plutex on 12/30/25.
//

#ifndef PLUENGINE_ENGINEOBJECTHANDLE_H
#define PLUENGINE_ENGINEOBJECTHANDLE_H
#include "PluEngine/Core.h"
#include "PluSTL/Hashers/Default.h"  // DefaultHash + HashCombine, used by the specialization below

namespace Plu
{
    struct EngineObjectHandle
    {
        UInt32 Index = 0;
        UInt32 Generation = 0;
        bool failed = true;

        bool operator==(const EngineObjectHandle& other) const
        {
            return Index == other.Index && Generation == other.Generation && failed == other.failed;
        }

        [[nodiscard]] String ToString() const
        {
            return String::Format("Handle: gen: {0}, idx: {1}", Generation, Index);
        }
    };

    // Hand-written because EngineObjectHandle has padding (two UInt32 plus a bool
    // is nine bytes in a twelve-byte struct), and DefaultHash's byte-wise fallback
    // would hash those uninitialized padding bytes along with the fields.
    template<>
    struct DefaultHash<EngineObjectHandle> {
        std::size_t operator()(const EngineObjectHandle& hdl) const noexcept {
            // Mix, never multiply: the previous version ended in `hash *= failHash`
            // where failHash was DefaultHash<UInt32>{}(0) for a valid handle — and the
            // MurmurHash3 finalizer of 0 is 0. Every non-failed handle therefore
            // hashed to 0 and landed in one bucket.
            std::size_t hash = DefaultHash<UInt32>{}(hdl.Index);
            HashCombine(hash, DefaultHash<UInt32>{}(hdl.Generation));
            HashCombine(hash, hdl.failed ? 0x9E3779B97F4A7C15ULL : 0ULL);
            return hash;
        }
    };
}

#endif //PLUENGINE_ENGINEOBJECTHANDLE_H