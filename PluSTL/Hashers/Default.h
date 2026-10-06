//
// Created by Plutex on 1/12/26.
//

#ifndef PLUENGINE_DEFAULT_H
#define PLUENGINE_DEFAULT_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace Plu
{
    // ========================================================================
    // HASH BUILDING BLOCKS
    // ========================================================================

    namespace Detail
    {
        inline constexpr std::size_t kFnvOffsetBasis = 14695981039346656037ULL;
        inline constexpr std::size_t kFnvPrime       = 1099511628211ULL;

        // FNV-1a over a byte range. The one definition every byte-wise hasher in
        // PluSTL goes through.
        [[nodiscard]] inline std::size_t HashBytes(const void* data, std::size_t length,
                                                   std::size_t seed = kFnvOffsetBasis) noexcept
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            std::size_t hash = seed;
            for (std::size_t i = 0; i < length; ++i)
            {
                hash ^= bytes[i];
                hash *= kFnvPrime;
            }
            return hash;
        }

        // MurmurHash3 finalizer.
        [[nodiscard]] inline std::size_t Mix32(std::uint32_t value) noexcept
        {
            value ^= value >> 16;
            value *= 0x85ebca6bu;
            value ^= value >> 13;
            value *= 0xc2b2ae35u;
            value ^= value >> 16;
            return value;
        }

        // splitmix64 finalizer.
        [[nodiscard]] inline std::size_t Mix64(std::uint64_t value) noexcept
        {
            value ^= value >> 30;
            value *= 0xbf58476d1ce4e5b9ULL;
            value ^= value >> 27;
            value *= 0x94d049bb133111ebULL;
            value ^= value >> 31;
            return static_cast<std::size_t>(value);
        }

        // Mixes any integral value, routed by width so every integer type — including
        // the ones with no dedicated specialization (char, bool, short, long long) —
        // gets a real avalanche rather than a byte loop.
        template<typename T>
        [[nodiscard]] inline std::size_t MixIntegral(T value) noexcept
        {
            using Unsigned = std::make_unsigned_t<T>;
            const auto raw = static_cast<Unsigned>(value);
            if constexpr (sizeof(T) <= 4)
            {
                return Mix32(static_cast<std::uint32_t>(raw));
            }
            else
            {
                return Mix64(static_cast<std::uint64_t>(raw));
            }
        }
    }

    // Folds an extra hash into a running seed. Use it to write a DefaultHash
    // specialization for a struct key instead of letting the byte-wise fallback
    // read the type's padding:
    //
    //   template<> struct DefaultHash<MyKey> {
    //       std::size_t operator()(const MyKey& k) const noexcept {
    //           std::size_t h = DefaultHash<UInt32>{}(k.A);
    //           HashCombine(h, DefaultHash<UInt32>{}(k.B));
    //           return h;
    //       }
    //   };
    inline void HashCombine(std::size_t& seed, std::size_t value) noexcept
    {
        // Boost's mixer, widened to 64 bits: never collapses when either side is 0,
        // which a plain xor or multiply does.
        seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    }

    // ========================================================================
    // DEFAULT HASH
    // ========================================================================
    // Handles enums, every integral width and the floating-point types directly.
    // Anything else falls back to hashing the object's bytes.
    //
    // WARNING about the byte-wise fallback: it reads sizeof(T) bytes, padding
    // included. For a struct with holes (e.g. { UInt32; UInt32; bool; } — three
    // padding bytes) two objects that compare equal can hash differently, because
    // the padding is whatever was on the stack. If your key is such a struct,
    // write a DefaultHash specialization with HashCombine. The static_assert below
    // only catches the louder mistake — a non-trivially-copyable key, where the
    // fallback would hash a pointer instead of the pointed-to value.
    template<typename T>
    struct DefaultHash
    {
        std::size_t operator()(const T& key) const noexcept
        {
            if constexpr (std::is_enum_v<T>)
            {
                return Detail::MixIntegral(static_cast<std::underlying_type_t<T>>(key));
            }
            else if constexpr (std::is_integral_v<T>)
            {
                return Detail::MixIntegral(key);
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                // Canonicalize before hashing the bits, or the container breaks its own
                // invariant: -0.0 == 0.0 yet their bit patterns differ, and every NaN
                // bit pattern differs from every other while none of them compare equal.
                T canonical = key;
                if (canonical == T{}) canonical = T{};           // -0.0 -> +0.0
                if (canonical != canonical) return 0x7ff8000000000000ULL; // all NaNs alike

                if constexpr (sizeof(T) <= 4)
                {
                    std::uint32_t bits = 0;
                    std::memcpy(&bits, &canonical, sizeof(bits));
                    return Detail::Mix32(bits);
                }
                else
                {
                    std::uint64_t bits = 0;
                    std::memcpy(&bits, &canonical, sizeof(bits));
                    return Detail::Mix64(bits);
                }
            }
            else
            {
                static_assert(std::is_trivially_copyable_v<T>,
                    "DefaultHash<T> falls back to hashing T's raw bytes, which is wrong for a type "
                    "that owns memory or has a custom operator== (it would hash the pointer, not the "
                    "value). Specialize Plu::DefaultHash<T> for this key type — see HashCombine.");
                return Detail::HashBytes(&key, sizeof(T));
            }
        }
    };

    // Pointer keys hash the address. Char pointers are deliberately excluded (see
    // Hashers/String.h) so that a C-string key hashes its text, not where it lives.
    template<typename T>
    struct DefaultHash<T*>
    {
        std::size_t operator()(T* ptr) const noexcept
        {
            return Detail::Mix64(reinterpret_cast<std::uintptr_t>(ptr));
        }
    };

    // std::pair keys hash member by member. Required, not just nicer: std::pair is
    // never trivially copyable (it has a user-provided operator=), so the byte-wise
    // fallback would reject it — and would read padding in e.g. pair<UInt64, UInt32>.
    // Each member needs a DefaultHash of its own; order matters, so (a, b) != (b, a).
    template<typename TFirst, typename TSecond>
    struct DefaultHash<std::pair<TFirst, TSecond>>
    {
        std::size_t operator()(const std::pair<TFirst, TSecond>& pair) const noexcept
        {
            std::size_t hash = DefaultHash<TFirst>{}(pair.first);
            HashCombine(hash, DefaultHash<TSecond>{}(pair.second));
            return hash;
        }
    };
}

#endif //PLUENGINE_DEFAULT_H
