//
// Created by Plutex on 1/12/26.
//

#ifndef PLUENGINE_STRINGHASHER_H
#define PLUENGINE_STRINGHASHER_H

#include <cstddef>
#include "PluSTL/Hashers/Default.h"

namespace Plu
{
    // Forward declarations for BasicString / BasicPath
    template<typename CharT, typename Allocator>
    class BasicString;

    template<typename CharT, typename Allocator>
    class BasicPath;

    // ========================================================================
    // STRING HASH SPECIALIZATIONS
    // ========================================================================
    // All of these hash the character *content*, so two strings that compare equal
    // hash equal regardless of where their buffers live (SSO vs heap). They go
    // through Detail::HashBytes so there is a single FNV implementation to fix.

    namespace Detail
    {
        template<typename CharT>
        [[nodiscard]] inline std::size_t HashCString(const CharT* str) noexcept
        {
            if (!str) return 0;

            std::size_t length = 0;
            while (str[length] != CharT{}) ++length;
            return HashBytes(str, length * sizeof(CharT));
        }
    }

    template<typename CharT, typename Allocator>
    struct DefaultHash<BasicString<CharT, Allocator>>
    {
        std::size_t operator()(const BasicString<CharT, Allocator>& str) const noexcept
        {
            return Detail::HashBytes(str.CStr(), str.Length() * sizeof(CharT));
        }
    };

    template<typename CharT, typename Allocator>
    struct DefaultHash<BasicPath<CharT, Allocator>>
    {
        std::size_t operator()(const BasicPath<CharT, Allocator>& path) const noexcept
        {
            // ToString() hands back a const reference to the stored string, so this
            // hashes in place. ToNativeString() would copy the whole path per lookup.
            const auto& str = path.ToString();
            return Detail::HashBytes(str.CStr(), str.Length() * sizeof(CharT));
        }
    };

    // C-string keys. Both the const and the non-const spellings are specialized:
    // without the non-const one, `char*` would fall through to DefaultHash<T*> and
    // silently hash the address instead of the text.
    template<> struct DefaultHash<const char*>
    {
        std::size_t operator()(const char* str) const noexcept { return Detail::HashCString(str); }
    };

    template<> struct DefaultHash<char*>
    {
        std::size_t operator()(const char* str) const noexcept { return Detail::HashCString(str); }
    };

    template<> struct DefaultHash<const wchar_t*>
    {
        std::size_t operator()(const wchar_t* str) const noexcept { return Detail::HashCString(str); }
    };

    template<> struct DefaultHash<wchar_t*>
    {
        std::size_t operator()(const wchar_t* str) const noexcept { return Detail::HashCString(str); }
    };
}

#endif //PLUENGINE_STRINGHASHER_H
