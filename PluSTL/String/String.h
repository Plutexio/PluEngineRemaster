#pragma once

#include <cstring>
#include <cwchar>
#include <cctype>
#include <cwctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
#include <type_traits>
#include "Allocators/Default.h"
#include "Array/Array.h"

namespace Plu
{
    // =============================================================================
    // UTF ENCODING HELPERS (używane przez FromNarrow/FromWide)
    // =============================================================================
    // Konwencja: narrow (char) = UTF-8, wide (wchar_t) = UTF-16 gdy sizeof(wchar_t)==2
    // (Windows) lub UTF-32 gdy ==4 (Linux). Nieprawidłowe sekwencje -> U+FFFD.

    namespace StringEncoding
    {
        inline constexpr char32_t ReplacementChar = 0xFFFD;

        // Dekoduje jedną sekwencję UTF-8 spod str (zakończonego nullem) do outCp.
        // Zwraca liczbę skonsumowanych bajtów (>= 1).
        inline std::size_t DecodeUtf8(const char* str, char32_t& outCp) noexcept {
            const auto* s = reinterpret_cast<const unsigned char*>(str);
            const unsigned char b0 = s[0];
            if (b0 < 0x80) {
                outCp = b0;
                return 1;
            }

            std::size_t len;
            char32_t cp;
            if      ((b0 & 0xE0) == 0xC0) { len = 2; cp = b0 & 0x1Fu; }
            else if ((b0 & 0xF0) == 0xE0) { len = 3; cp = b0 & 0x0Fu; }
            else if ((b0 & 0xF8) == 0xF0) { len = 4; cp = b0 & 0x07u; }
            else { outCp = ReplacementChar; return 1; }

            for (std::size_t i = 1; i < len; ++i) {
                if ((s[i] & 0xC0) != 0x80) {
                    outCp = ReplacementChar;
                    return i;
                }
                cp = (cp << 6) | (s[i] & 0x3Fu);
            }

            // Odrzuć overlong encoding, surrogaty i wartości poza zakresem Unicode
            const bool overlong = (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000);
            if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) || overlong) {
                outCp = ReplacementChar;
            } else {
                outCp = cp;
            }
            return len;
        }

        [[nodiscard]] inline std::size_t Utf8EncodedLength(char32_t cp) noexcept {
            return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        }

        // Zapisuje cp jako UTF-8 do out (bufor min. 4 bajty); zwraca liczbę bajtów.
        inline std::size_t EncodeUtf8(char32_t cp, char* out) noexcept {
            if (cp < 0x80) {
                out[0] = static_cast<char>(cp);
                return 1;
            }
            if (cp < 0x800) {
                out[0] = static_cast<char>(0xC0 | (cp >> 6));
                out[1] = static_cast<char>(0x80 | (cp & 0x3F));
                return 2;
            }
            if (cp < 0x10000) {
                out[0] = static_cast<char>(0xE0 | (cp >> 12));
                out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out[2] = static_cast<char>(0x80 | (cp & 0x3F));
                return 3;
            }
            out[0] = static_cast<char>(0xF0 | (cp >> 18));
            out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[3] = static_cast<char>(0x80 | (cp & 0x3F));
            return 4;
        }

        // Odczytuje jeden codepoint spod *pp i przesuwa wskaźnik (o 2 jednostki
        // przy poprawnej parze surrogatów na 2-bajtowym wchar_t).
        inline char32_t DecodeWide(const wchar_t** pp) noexcept {
            const wchar_t* p = *pp;
            auto cp = static_cast<char32_t>(static_cast<std::uint32_t>(*p++));
            if constexpr (sizeof(wchar_t) == 2) {
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    const auto low = static_cast<char32_t>(static_cast<std::uint32_t>(*p));
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        ++p;
                    } else {
                        cp = ReplacementChar; // niesparowany high surrogate
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    cp = ReplacementChar; // samotny low surrogate
                }
            } else {
                if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                    cp = ReplacementChar;
                }
            }
            *pp = p;
            return cp;
        }

        // Liczba jednostek wchar_t potrzebnych na cp (para surrogatów na Windowsie).
        [[nodiscard]] inline std::size_t WideEncodedLength(char32_t cp) noexcept {
            return (sizeof(wchar_t) == 2 && cp >= 0x10000) ? 2 : 1;
        }

        // Zapisuje cp do out (bufor min. 2 jednostki); zwraca liczbę jednostek.
        inline std::size_t EncodeWide(char32_t cp, wchar_t* out) noexcept {
            if constexpr (sizeof(wchar_t) == 2) {
                if (cp >= 0x10000) {
                    cp -= 0x10000;
                    out[0] = static_cast<wchar_t>(0xD800 | (cp >> 10));
                    out[1] = static_cast<wchar_t>(0xDC00 | (cp & 0x3FF));
                    return 2;
                }
            }
            out[0] = static_cast<wchar_t>(cp);
            return 1;
        }
    }

    // =============================================================================
    // BasicString CLASS
    // =============================================================================

    template<typename CharT, typename Allocator = DefaultAllocator<CharT>>
    class BasicString {
    public:
        using ValueType = CharT;
        using SizeType = std::size_t;
        static constexpr SizeType Npos = static_cast<SizeType>(-1);

        // Small BasicString Optimization - 23 chars for char, 11 for wchar_t (typical)
        static constexpr SizeType SsoCapacity = (24 / sizeof(CharT)) - 1;

    private:
        // SSO: Store short strings inline to avoid heap allocation
        union {
            CharT mSsoBuffer[SsoCapacity + 1];
            CharT* mHeapData;
        };

        SizeType mLength;
        SizeType mCapacity;
        Allocator mAllocator;

        // Flag to determine if using heap (stored in LSB of capacity for space efficiency)
        [[nodiscard]] bool IsHeap() const noexcept {
            return mCapacity > SsoCapacity;
        }

        [[nodiscard]] CharT* GetData() noexcept {
            return IsHeap() ? mHeapData : mSsoBuffer;
        }

        [[nodiscard]] const CharT* GetData() const noexcept {
            return IsHeap() ? mHeapData : mSsoBuffer;
        }

        // BasicString utility functions - conditionally compiled based on CharT
        static SizeType StrLen(const CharT* str) noexcept {
            if constexpr (std::is_same_v<CharT, char>) {
                return std::strlen(str);
            } else if constexpr (std::is_same_v<CharT, wchar_t>) {
                return std::wcslen(str);
            }
        }

        static void StrCopy(CharT* dest, const CharT* src, SizeType count) noexcept {
            if constexpr (std::is_same_v<CharT, char>) {
                std::memcpy(dest, src, count * sizeof(CharT));
            } else if constexpr (std::is_same_v<CharT, wchar_t>) {
                std::wmemcpy(dest, src, count);
            }
        }

        // Same as StrCopy, but valid when the two ranges overlap. Shifting characters
        // within one buffer — what Insert and Remove do — always overlaps, and memcpy
        // is undefined there however well it happens to behave in practice.
        static void StrMove(CharT* dest, const CharT* src, SizeType count) noexcept {
            if constexpr (std::is_same_v<CharT, char>) {
                std::memmove(dest, src, count * sizeof(CharT));
            } else if constexpr (std::is_same_v<CharT, wchar_t>) {
                std::wmemmove(dest, src, count);
            }
        }

        static int StrCompare(const CharT* s1, const CharT* s2, SizeType count) noexcept {
            if constexpr (std::is_same_v<CharT, char>) {
                return std::memcmp(s1, s2, count * sizeof(CharT));
            } else if constexpr (std::is_same_v<CharT, wchar_t>) {
                return std::wmemcmp(s1, s2, count);
            }
        }

        static CharT ToLowerChar(CharT c) noexcept {
            if constexpr (std::is_same_v<CharT, char>) {
                return static_cast<CharT>(std::tolower(static_cast<unsigned char>(c)));
            } else if constexpr (std::is_same_v<CharT, wchar_t>) {
                return static_cast<CharT>(std::towlower(static_cast<std::wint_t>(c)));
            }
        }

        static CharT ToUpperChar(CharT c) noexcept {
            if constexpr (std::is_same_v<CharT, char>) {
                return static_cast<CharT>(std::toupper(static_cast<unsigned char>(c)));
            } else if constexpr (std::is_same_v<CharT, wchar_t>) {
                return static_cast<CharT>(std::towupper(static_cast<std::wint_t>(c)));
            }
        }

        void AllocateHeap(SizeType capacity) noexcept {
            mHeapData = mAllocator.Allocate(capacity + 1);
            mCapacity = capacity;
        }

        void DeallocateHeap() noexcept {
            if (IsHeap() && mHeapData) {
                mAllocator.Deallocate(mHeapData, mCapacity + 1);
            }
        }

        // Grow capacity using 1.5x growth factor (common in game engines)
        SizeType CalculateGrowth(SizeType newLength) const noexcept {
            SizeType newCapacity = mCapacity + mCapacity / 2;
            return (newCapacity < newLength) ? newLength : newCapacity;
        }

    public:
        // =========================================================================
        // ITERATORS
        // =========================================================================

        class ConstIterator {
        public:
            using ValueType     = const CharT;
            using PointerType   = const CharT*;
            using ReferenceType = const CharT&;

            explicit ConstIterator(PointerType ptr) noexcept : mPtr(ptr) {}

            ReferenceType operator*()  const noexcept { return *mPtr; }
            PointerType   operator->() const noexcept { return mPtr; }

            ConstIterator& operator++()    noexcept { ++mPtr; return *this; }
            ConstIterator  operator++(int) noexcept { ConstIterator tmp = *this; ++mPtr; return tmp; }
            ConstIterator& operator--()    noexcept { --mPtr; return *this; }
            ConstIterator  operator--(int) noexcept { ConstIterator tmp = *this; --mPtr; return tmp; }

            ConstIterator operator+(std::ptrdiff_t n) const noexcept { return ConstIterator(mPtr + n); }
            ConstIterator operator-(std::ptrdiff_t n) const noexcept { return ConstIterator(mPtr - n); }

            bool operator==(const ConstIterator& other) const noexcept { return mPtr == other.mPtr; }
            bool operator!=(const ConstIterator& other) const noexcept { return mPtr != other.mPtr; }
            bool operator< (const ConstIterator& other) const noexcept { return mPtr <  other.mPtr; }
            bool operator> (const ConstIterator& other) const noexcept { return mPtr >  other.mPtr; }

        private:
            PointerType mPtr;
        };

        class ConstReverseIterator {
        public:
            using ValueType     = const CharT;
            using PointerType   = const CharT*;
            using ReferenceType = const CharT&;

            explicit ConstReverseIterator(PointerType ptr) noexcept : mPtr(ptr) {}

            ReferenceType operator*()  const noexcept { return *mPtr; }
            PointerType   operator->() const noexcept { return mPtr; }

            ConstReverseIterator& operator++()    noexcept { --mPtr; return *this; }
            ConstReverseIterator  operator++(int) noexcept { ConstReverseIterator tmp = *this; --mPtr; return tmp; }
            ConstReverseIterator& operator--()    noexcept { ++mPtr; return *this; }
            ConstReverseIterator  operator--(int) noexcept { ConstReverseIterator tmp = *this; ++mPtr; return tmp; }

            ConstReverseIterator operator+(std::ptrdiff_t n) const noexcept { return ConstReverseIterator(mPtr - n); }
            ConstReverseIterator operator-(std::ptrdiff_t n) const noexcept { return ConstReverseIterator(mPtr + n); }

            bool operator==(const ConstReverseIterator& other) const noexcept { return mPtr == other.mPtr; }
            bool operator!=(const ConstReverseIterator& other) const noexcept { return mPtr != other.mPtr; }

        private:
            PointerType mPtr;
        };

        // Forward iteration
        [[nodiscard]] ConstIterator begin()  const noexcept { return ConstIterator(GetData()); }
        [[nodiscard]] ConstIterator end()    const noexcept { return ConstIterator(GetData() + mLength); }
        [[nodiscard]] ConstIterator cbegin() const noexcept { return begin(); }
        [[nodiscard]] ConstIterator cend()   const noexcept { return end(); }

        // Reverse iteration
        [[nodiscard]] ConstReverseIterator rbegin()  const noexcept { return ConstReverseIterator(GetData() + mLength - 1); }
        [[nodiscard]] ConstReverseIterator rend()    const noexcept { return ConstReverseIterator(GetData() - 1); }
        [[nodiscard]] ConstReverseIterator crbegin() const noexcept { return rbegin(); }
        [[nodiscard]] ConstReverseIterator crend()   const noexcept { return rend(); }

        // =========================================================================
        // CONSTRUCTORS & DESTRUCTOR
        // =========================================================================

        BasicString() noexcept : mLength(0), mCapacity(SsoCapacity) {
            mSsoBuffer[0] = CharT{0};
        }

        BasicString(const CharT* str) noexcept : BasicString() {
            if (!str) return;

            SizeType len = StrLen(str);
            if (len <= SsoCapacity) {
                StrCopy(mSsoBuffer, str, len);
                mSsoBuffer[len] = CharT{0};
                mLength = len;
            } else {
                AllocateHeap(len);
                if (mHeapData) {
                    StrCopy(mHeapData, str, len);
                    mHeapData[len] = CharT{0};
                    mLength = len;
                }
            }
        }

        BasicString(const CharT* str, SizeType length) noexcept : BasicString() {
            if (!str || length == 0) return;

            if (length <= SsoCapacity) {
                StrCopy(mSsoBuffer, str, length);
                mSsoBuffer[length] = CharT{0};
                mLength = length;
            } else {
                AllocateHeap(length);
                if (mHeapData) {
                    StrCopy(mHeapData, str, length);
                    mHeapData[length] = CharT{0};
                    mLength = length;
                }
            }
        }

        // Copy constructor
        BasicString(const BasicString& other) noexcept : mLength(other.mLength), mCapacity(other.mCapacity) {
            if (other.IsHeap()) {
                AllocateHeap(other.mCapacity);
                if (mHeapData) {
                    StrCopy(mHeapData, other.mHeapData, mLength + 1);
                }
            } else {
                StrCopy(mSsoBuffer, other.mSsoBuffer, mLength + 1);
            }
        }

        // Move constructor
        BasicString(BasicString&& other) noexcept : mLength(other.mLength), mCapacity(other.mCapacity) {
            if (other.IsHeap()) {
                mHeapData = other.mHeapData;
                other.mHeapData = nullptr;
                other.mLength = 0;
                other.mCapacity = SsoCapacity;
            } else {
                StrCopy(mSsoBuffer, other.mSsoBuffer, mLength + 1);
            }
        }

        ~BasicString() noexcept {
            DeallocateHeap();
        }

        // =========================================================================
        // ASSIGNMENT OPERATORS
        // =========================================================================

        BasicString& operator=(const BasicString& other) noexcept {
            if (this != &other) {
                DeallocateHeap();

                mLength = other.mLength;
                mCapacity = other.mCapacity;

                if (other.IsHeap()) {
                    AllocateHeap(other.mCapacity);
                    if (mHeapData) {
                        StrCopy(mHeapData, other.mHeapData, mLength + 1);
                    }
                } else {
                    StrCopy(mSsoBuffer, other.mSsoBuffer, mLength + 1);
                }
            }
            return *this;
        }

        BasicString& operator=(BasicString&& other) noexcept {
            if (this != &other) {
                DeallocateHeap();

                mLength = other.mLength;
                mCapacity = other.mCapacity;

                if (other.IsHeap()) {
                    mHeapData = other.mHeapData;
                    other.mHeapData = nullptr;
                    other.mLength = 0;
                    other.mCapacity = SsoCapacity;
                } else {
                    StrCopy(mSsoBuffer, other.mSsoBuffer, mLength + 1);
                }
            }
            return *this;
        }

        // =========================================================================
        // BASIC OPERATIONS
        // =========================================================================

        [[nodiscard]] SizeType Length() const noexcept { return mLength; }
        [[nodiscard]] SizeType Capacity() const noexcept { return mCapacity; }
        [[nodiscard]] bool IsEmpty() const noexcept { return mLength == 0; }
        [[nodiscard]] const CharT* CStr() const noexcept { return GetData(); }

        void Clear() noexcept {
            mLength = 0;
            GetData()[0] = CharT{0};
        }

        [[nodiscard]] CharT& operator[](SizeType index) noexcept {
            return GetData()[index];
        }

        [[nodiscard]] const CharT& operator[](SizeType index) const noexcept {
            return GetData()[index];
        }

        // Replace single character at given index (no-op if index out of range)
        void ReplaceAt(SizeType index, CharT newChar) noexcept {
            if (index < mLength) {
                GetData()[index] = newChar;
            }
        }

        void Reserve(SizeType newCapacity) noexcept {
            if (newCapacity <= mCapacity) return;

            CharT* newData = mAllocator.Allocate(newCapacity + 1);
            if (!newData) return;

            StrCopy(newData, GetData(), mLength + 1);
            DeallocateHeap();

            mHeapData = newData;
            mCapacity = newCapacity;
        }

        // =========================================================================
        // SEARCH OPERATIONS
        // =========================================================================

        [[nodiscard]] SizeType Find(CharT c, SizeType startPos = 0) const noexcept {
            if (startPos >= mLength) return Npos;

            const CharT* data = GetData();
            for (SizeType i = startPos; i < mLength; ++i) {
                if (data[i] == c) return i;
            }
            return Npos;
        }

        [[nodiscard]] SizeType Find(const CharT* substr, SizeType startPos = 0) const noexcept {
            if (!substr || startPos >= mLength) return Npos;

            SizeType subLen = StrLen(substr);
            if (subLen == 0 || subLen > mLength - startPos) return Npos;

            const CharT* data = GetData();
            for (SizeType i = startPos; i <= mLength - subLen; ++i) {
                if (StrCompare(data + i, substr, subLen) == 0) {
                    return i;
                }
            }
            return Npos;
        }

        [[nodiscard]] SizeType RFind(CharT c, SizeType startPos = Npos) const noexcept {
            if (mLength == 0) return Npos;

            SizeType pos = (startPos == Npos || startPos >= mLength) ? mLength - 1 : startPos;
            const CharT* data = GetData();

            for (SizeType i = pos + 1; i > 0; --i) {
                if (data[i - 1] == c) return i - 1;
            }
            return Npos;
        }

        [[nodiscard]] bool Contains(const CharT* substr) const noexcept {
            return Find(substr) != Npos;
        }

        [[nodiscard]] bool StartsWith(const CharT* prefix) const noexcept {
            if (!prefix) return false;
            SizeType prefixLen = StrLen(prefix);
            if (prefixLen > mLength) return false;
            return StrCompare(GetData(), prefix, prefixLen) == 0;
        }

        [[nodiscard]] bool EndsWith(const CharT* suffix) const noexcept {
            if (!suffix) return false;
            SizeType suffixLen = StrLen(suffix);
            if (suffixLen > mLength) return false;
            return StrCompare(GetData() + mLength - suffixLen, suffix, suffixLen) == 0;
        }

        // =========================================================================
        // SUBSTRING & MODIFICATION
        // =========================================================================

        [[nodiscard]] DynamicArray<BasicString> Split(CharT delimiter) const noexcept {
            DynamicArray<BasicString> tokens;
            if (IsEmpty()) return tokens;

            SizeType start = 0;
            SizeType end = Find(delimiter);

            while (end != Npos) {
                // Dodajemy fragment od startu do znalezionego ogranicznika
                tokens.PushBack(Substring(start, end - start));
                start = end + 1;
                end = Find(delimiter, start);
            }

            // Dodajemy ostatni fragment (po ostatnim ograniczniku)
            tokens.PushBack(Substring(start));

            return tokens;
        }

        [[nodiscard]] DynamicArray<BasicString> Split(const CharT* delimiter) const noexcept {
            DynamicArray<BasicString> tokens;
            if (IsEmpty() || !delimiter) return tokens;

            SizeType delimiterLen = StrLen(delimiter);
            if (delimiterLen == 0) {
                tokens.PushBack(*this);
                return tokens;
            }

            SizeType start = 0;
            SizeType end = Find(delimiter);

            while (end != Npos) {
                tokens.PushBack(Substring(start, end - start));
                start = end + delimiterLen;
                end = Find(delimiter, start);
            }

            tokens.PushBack(Substring(start));

            return tokens;
        }

        [[nodiscard]] BasicString Substring(SizeType start, SizeType length = Npos) const noexcept {
            if (start >= mLength) return BasicString();

            SizeType actualLength = (length == Npos || start + length > mLength)
                                    ? mLength - start : length;
            return BasicString(GetData() + start, actualLength);
        }

        void Append(const CharT* str) noexcept {
            if (!str) return;

            SizeType addLen = StrLen(str);
            if (addLen == 0) return;

            SizeType newLength = mLength + addLen;
            if (newLength > mCapacity) {
                Reserve(CalculateGrowth(newLength));
            }

            StrCopy(GetData() + mLength, str, addLen);
            mLength = newLength;
            GetData()[mLength] = CharT{0};
        }

        void Append(const BasicString& other) noexcept {
            Append(other.CStr());
        }

        void Insert(SizeType pos, const CharT* str) noexcept {
            if (!str || pos > mLength) return;

            SizeType insertLen = StrLen(str);
            if (insertLen == 0) return;

            SizeType newLength = mLength + insertLen;
            if (newLength > mCapacity) {
                Reserve(CalculateGrowth(newLength));
            }

            CharT* data = GetData();
            // Shift the tail right, terminator included (hence the + 1). This used to be
            // a backward loop guarded by `i > 0`, which skipped index 0 — so inserting at
            // position 0 dropped the first character: "abc".Insert(0, "XY") gave "XYcbc".
            StrMove(data + pos + insertLen, data + pos, mLength - pos + 1);

            // Insert new content
            StrCopy(data + pos, str, insertLen);
            mLength = newLength;
            data[mLength] = CharT{0};
        }

        void Remove(SizeType start, SizeType length = Npos) noexcept {
            if (start >= mLength) return;

            SizeType actualLength = (length == Npos || start + length > mLength)
                                    ? mLength - start : length;

            CharT* data = GetData();
            SizeType remaining = mLength - start - actualLength;

            if (remaining > 0) {
                StrMove(data + start, data + start + actualLength, remaining);
            }

            mLength -= actualLength;
            data[mLength] = CharT{0};
        }

        void Replace(const CharT* oldStr, const CharT* newStr) noexcept {
            if (!oldStr || !newStr) return;

            SizeType oldLen = StrLen(oldStr);
            SizeType newLen = StrLen(newStr);

            SizeType pos = Find(oldStr);
            if (pos == Npos) return;

            Remove(pos, oldLen);
            Insert(pos, newStr);
        }

        // =========================================================================
        // STRIP
        // =========================================================================

        // Python-style strip: removes leading/trailing characters found in `chars`.
        // `chars == nullptr` strips whitespace (space, \t, \n, \r).
        void StripLeftInPlace(const CharT* chars = nullptr) noexcept {
            const CharT* data = GetData();
            SizeType start = 0;
            while (start < mLength && IsStripChar(data[start], chars)) ++start;
            if (start > 0) Remove(0, start);
        }

        void StripRightInPlace(const CharT* chars = nullptr) noexcept {
            CharT* data = GetData();
            SizeType end = mLength;
            while (end > 0 && IsStripChar(data[end - 1], chars)) --end;
            if (end < mLength) {
                mLength = end;
                data[mLength] = CharT{0};
            }
        }

        void StripInPlace(const CharT* chars = nullptr) noexcept {
            StripRightInPlace(chars);
            StripLeftInPlace(chars);
        }

        [[nodiscard]] BasicString StripLeft(const CharT* chars = nullptr) const noexcept {
            const CharT* data = GetData();
            SizeType start = 0;
            while (start < mLength && IsStripChar(data[start], chars)) ++start;
            return BasicString(data + start, mLength - start);
        }

        [[nodiscard]] BasicString StripRight(const CharT* chars = nullptr) const noexcept {
            const CharT* data = GetData();
            SizeType end = mLength;
            while (end > 0 && IsStripChar(data[end - 1], chars)) --end;
            return BasicString(data, end);
        }

        [[nodiscard]] BasicString Strip(const CharT* chars = nullptr) const noexcept {
            const CharT* data = GetData();
            SizeType start = 0;
            SizeType end = mLength;
            while (start < end && IsStripChar(data[start], chars)) ++start;
            while (end > start && IsStripChar(data[end - 1], chars)) --end;
            return BasicString(data + start, end - start);
        }

        // =========================================================================
        // CASE OPERATIONS
        // =========================================================================

        void ToLowerInPlace() noexcept {
            CharT* data = GetData();
            for (SizeType i = 0; i < mLength; ++i) {
                data[i] = ToLowerChar(data[i]);
            }
        }

        void ToUpperInPlace() noexcept {
            CharT* data = GetData();
            for (SizeType i = 0; i < mLength; ++i) {
                data[i] = ToUpperChar(data[i]);
            }
        }

        [[nodiscard]] BasicString ToLower() const noexcept {
            BasicString result(*this);
            result.ToLowerInPlace();
            return result;
        }

        [[nodiscard]] BasicString ToUpper() const noexcept {
            BasicString result(*this);
            result.ToUpperInPlace();
            return result;
        }

        // =========================================================================
        // NUMERIC CONVERSION TO BasicString
        // =========================================================================

        // Convert integer to BasicString
        template<typename IntT>
        static BasicString FromInt(IntT value) noexcept {
            static_assert(std::is_integral_v<IntT>, "FromInt requires integral type");

            // Buffer size: max digits for 64-bit int (20) + sign + null
            CharT buffer[32];
            CharT* ptr = buffer + 31;
            *ptr = CharT{0};

            bool negative = false;
            if constexpr (std::is_signed_v<IntT>) {
                if (value < 0) {
                    negative = true;
                    value = -value;
                }
            }

            // Convert digits in reverse
            using UnsignedT = typename std::make_unsigned<IntT>::type;
            UnsignedT uvalue = static_cast<UnsignedT>(value);

            if (uvalue == 0) {
                *(--ptr) = GetDigitChar(0);
            } else {
                while (uvalue > 0) {
                    *(--ptr) = GetDigitChar(uvalue % 10);
                    uvalue /= 10;
                }
            }

            if (negative) {
                *(--ptr) = GetMinusChar();
            }

            return BasicString(ptr);
        }

        // Convert float/double to BasicString with specified precision
        template<typename FloatT>
        static BasicString FromFloat(FloatT value, int precision = 6) noexcept {
            static_assert(std::is_floating_point_v<FloatT>, "FromFloat requires floating point type");

            // Handle special cases
            if (value != value)
                return BasicString(GetNaNStr());
            if (value == std::numeric_limits<FloatT>::infinity())
                return BasicString(GetInfStr());
            if (value == -std::numeric_limits<FloatT>::infinity())
                return BasicString(GetNegInfStr());

            // Use snprintf/swprintf — handles sign, rounding and precision correctly.
            // Precision clamped to [0, 20] to keep the buffer size bounded.
            int prec = (precision < 0) ? 0 : (precision > 20 ? 20 : precision);

            // Buffer: sign(1) + 20 integer digits + '.'(1) + 20 fraction digits + '\0' = 43
            CharT buf[64];

            if constexpr (std::is_same_v<CharT, char>) {
                std::snprintf(buf, sizeof(buf), "%.*f", prec, static_cast<double>(value));
            } else {
                std::swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%.*f", prec, static_cast<double>(value));
            }

            return BasicString(buf);
        }

        // Convert boolean to BasicString
        static BasicString FromBool(bool value) noexcept {
            return value ? BasicString(GetTrueStr()) : BasicString(GetFalseStr());
        }

        // Convert pointer to hex BasicString
        template<typename T>
        static BasicString FromPointer(T* ptr) noexcept {
            if (!ptr) {
                return BasicString(GetNullptrStr());
            }

            BasicString result(GetHexPrefix());

            uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
            CharT buffer[32];
            CharT* bufPtr = buffer + 31;
            *bufPtr = CharT{0};

            // Convert to hex
            do {
                int digit = addr & 0xF;
                *(--bufPtr) = GetHexChar(digit);
                addr >>= 4;
            } while (addr > 0);

            result += bufPtr;
            return result;
        }

        // =========================================================================
        // NUMERIC CONVERSION FROM BasicString
        // =========================================================================

        // Convert BasicString to integer
        template<typename IntT = int>
        [[nodiscard]] IntT ToInt(bool* success = nullptr) const noexcept {
            static_assert(std::is_integral_v<IntT>, "ToInt requires integral type");

            if (success) *success = false;
            if (IsEmpty()) return IntT{0};

            const CharT* str = GetData();
            SizeType pos = 0;

            // Skip whitespace
            while (pos < mLength && IsWhitespace(str[pos])) ++pos;
            if (pos >= mLength) return IntT{0};

            // Check sign
            bool negative = false;
            if (str[pos] == GetMinusChar()) {
                if constexpr (std::is_unsigned_v<IntT>) {
                    return IntT{0}; // Unsigned can't be negative
                }
                negative = true;
                ++pos;
            } else if (str[pos] == GetPlusChar()) {
                ++pos;
            }

            // Parse digits
            IntT result = 0;
            bool hasDigits = false;

            while (pos < mLength && IsDigit(str[pos])) {
                int digit = str[pos] - GetDigitChar(0);
                result = result * 10 + digit;
                ++pos;
                hasDigits = true;
            }

            if (success) *success = hasDigits;
            return negative ? -result : result;
        }

        // Convert BasicString to float
        [[nodiscard]] float ToFloat(bool* success = nullptr) const noexcept {
            return static_cast<float>(ToDouble(success));
        }

        // Convert BasicString to double
        [[nodiscard]] double ToDouble(bool* success = nullptr) const noexcept {
            if (success) *success = false;
            if (IsEmpty()) return 0.0;

            const CharT* str = GetData();
            SizeType pos = 0;

            // Skip whitespace
            while (pos < mLength && IsWhitespace(str[pos])) ++pos;
            if (pos >= mLength) return 0.0;

            // Check for special values
            if (StartsWithAt(GetNaNStr(), pos)) {
                if (success) *success = true;
                return std::numeric_limits<double>::quiet_NaN();
            }
            if (StartsWithAt(GetInfStr(), pos)) {
                if (success) *success = true;
                return std::numeric_limits<double>::infinity();
            }

            // Parse sign
            bool negative = false;
            if (str[pos] == GetMinusChar()) {
                negative = true;
                ++pos;
            } else if (str[pos] == GetPlusChar()) {
                ++pos;
            }

            // Parse integer part
            double result = 0.0;
            bool hasDigits = false;

            while (pos < mLength && IsDigit(str[pos])) {
                result = result * 10.0 + (str[pos] - GetDigitChar(0));
                ++pos;
                hasDigits = true;
            }

            // Parse fractional part
            if (pos < mLength && str[pos] == GetDecimalChar()) {
                ++pos;
                double fraction = 0.0;
                double divisor = 10.0;

                while (pos < mLength && IsDigit(str[pos])) {
                    fraction += (str[pos] - GetDigitChar(0)) / divisor;
                    divisor *= 10.0;
                    ++pos;
                    hasDigits = true;
                }

                result += fraction;
            }

            if (success) *success = hasDigits;
            return negative ? -result : result;
        }

        // Convert BasicString to boolean
        [[nodiscard]] bool ToBool() const noexcept {
            if (IsEmpty()) return false;

            // Check for "true" (case-insensitive)
            if (CompareIgnoreCaseWith(GetTrueStr())) return true;

            // Check for "1"
            if (mLength == 1 && GetData()[0] == GetDigitChar(1)) return true;

            return false;
        }

    private:
        // Helper functions for character literals (compile-time selection based on CharT)
        static constexpr CharT GetDigitChar(int digit) noexcept {
            if constexpr (std::is_same_v<CharT, char>) {
                return '0' + static_cast<char>(digit);
            } else {
                return L'0' + static_cast<wchar_t>(digit);
            }
        }

        static constexpr CharT GetHexChar(int digit) noexcept {
            if constexpr (std::is_same_v<CharT, char>) {
                return (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
            } else {
                return (digit < 10) ? (L'0' + digit) : (L'a' + digit - 10);
            }
        }

        static constexpr CharT GetMinusChar() noexcept {
            return (std::is_same_v<CharT, char>) ? '-' : L'-';
        }

        static constexpr CharT GetPlusChar() noexcept {
            return (std::is_same_v<CharT, char>) ? '+' : L'+';
        }

        static constexpr CharT GetDecimalChar() noexcept {
            return (std::is_same_v<CharT, char>) ? '.' : L'.';
        }

        static constexpr const CharT* GetTrueStr() noexcept {
            if constexpr (std::is_same_v<CharT, char>) { return "true"; }
            else { return L"true"; }
        }

        static constexpr const CharT* GetFalseStr() noexcept {
            if constexpr (std::is_same_v<CharT, char>) { return "false"; }
            else { return L"false"; }
        }

        static constexpr const CharT* GetNaNStr() noexcept {
            if constexpr (std::is_same_v<CharT, char>) {
                return "nan";
            } else {
                return L"nan";
            }
        }

        static constexpr const CharT* GetInfStr() noexcept {
            if constexpr (std::is_same_v<CharT, char>) {
                return "inf";
            } else {
                return L"inf";
            }
        }

        static constexpr const CharT* GetNegInfStr() noexcept {
            if constexpr (std::is_same_v<CharT, char>) {
                return "-inf";
            } else {
                return L"-inf";
            }
        }

        static constexpr const CharT* GetNullptrStr() noexcept {
            if constexpr (std::is_same_v<CharT, char>) { return "nullptr"; }
            else { return L"nullptr"; }
        }

        static constexpr const CharT* GetHexPrefix() noexcept {
            if constexpr (std::is_same_v<CharT, char>) { return "0x"; }
            else { return L"0x"; }
        }

        static bool IsWhitespace(CharT c) noexcept {
            if constexpr (std::is_same_v<CharT, char>) {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r';
            } else {
                return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r';
            }
        }

        static bool IsStripChar(CharT c, const CharT* chars) noexcept {
            if (!chars) return IsWhitespace(c);
            for (; *chars; ++chars) {
                if (*chars == c) return true;
            }
            return false;
        }

        static bool IsDigit(CharT c) noexcept {
            if constexpr (std::is_same_v<CharT, char>) {
                return c >= '0' && c <= '9';
            } else {
                return c >= L'0' && c <= L'9';
            }
        }

        bool StartsWithAt(const CharT* prefix, SizeType pos) const noexcept {
            if (!prefix) return false;
            SizeType prefixLen = StrLen(prefix);
            if (pos + prefixLen > mLength) return false;
            return StrCompare(GetData() + pos, prefix, prefixLen) == 0;
        }

        bool CompareIgnoreCaseWith(const CharT* other) const noexcept {
            if (!other) return false;
            SizeType otherLen = StrLen(other);
            if (mLength != otherLen) return false;

            const CharT* data = GetData();
            for (SizeType i = 0; i < mLength; ++i) {
                if (ToLowerChar(data[i]) != ToLowerChar(other[i])) {
                    return false;
                }
            }
            return true;
        }

    public:
        // =========================================================================
        // ENCODING CONVERSION (char <-> wchar_t)
        // =========================================================================

        // Convert from narrow (char, UTF-8) BasicString to this BasicString type
        template<typename SrcCharT = char>
        static BasicString FromNarrow(const SrcCharT* str) noexcept {
            static_assert(std::is_same_v<SrcCharT, char>, "FromNarrow expects char*");

            if constexpr (std::is_same_v<CharT, char>) {
                // char -> char: direct copy
                return BasicString(str);
            } else if constexpr (std::is_same_v<CharT, wchar_t>) {
                // char (UTF-8) -> wchar_t (UTF-16/32)
                if (!str) return BasicString();

                // Przebieg 1: policz jednostki wchar_t
                SizeType outLen = 0;
                for (const char* p = str; *p;) {
                    char32_t cp;
                    p += StringEncoding::DecodeUtf8(p, cp);
                    outLen += StringEncoding::WideEncodedLength(cp);
                }

                BasicString result;
                if (outLen > SsoCapacity) {
                    result.AllocateHeap(outLen);
                }

                // Przebieg 2: zapisz
                wchar_t* dest = result.GetData();
                for (const char* p = str; *p;) {
                    char32_t cp;
                    p += StringEncoding::DecodeUtf8(p, cp);
                    dest += StringEncoding::EncodeWide(cp, dest);
                }
                *dest = wchar_t{0};
                result.mLength = outLen;

                return result;
            }
        }

        // Convert from wide (wchar_t, UTF-16/32) BasicString to this BasicString type
        template<typename SrcCharT = wchar_t>
        static BasicString FromWide(const SrcCharT* str) noexcept {
            static_assert(std::is_same_v<SrcCharT, wchar_t>, "FromWide expects wchar_t*");

            if constexpr (std::is_same_v<CharT, wchar_t>) {
                // wchar_t -> wchar_t: direct copy
                return BasicString(str);
            } else if constexpr (std::is_same_v<CharT, char>) {
                // wchar_t (UTF-16/32) -> char (UTF-8)
                if (!str) return BasicString();

                // Przebieg 1: policz bajty UTF-8
                SizeType outLen = 0;
                for (const wchar_t* p = str; *p;) {
                    outLen += StringEncoding::Utf8EncodedLength(StringEncoding::DecodeWide(&p));
                }

                BasicString result;
                if (outLen > SsoCapacity) {
                    result.AllocateHeap(outLen);
                }

                // Przebieg 2: zapisz
                char* dest = result.GetData();
                for (const wchar_t* p = str; *p;) {
                    dest += StringEncoding::EncodeUtf8(StringEncoding::DecodeWide(&p), dest);
                }
                *dest = char{0};
                result.mLength = outLen;

                return result;
            }
        }

        template<typename SrcCharT = char, typename SrcAlloc = DefaultAllocator<char>>
        static BasicString FromNarrow(const BasicString<char, SrcAlloc>& str) noexcept {
            return FromNarrow(str.CStr());
        }

        // Przeciążenie dla BasicString<wchar_t>
        template<typename SrcCharT = wchar_t, typename SrcAlloc = DefaultAllocator<wchar_t>>
        static BasicString FromWide(const BasicString<wchar_t, SrcAlloc>& str) noexcept {
            return FromWide(str.CStr());
        }

        // Convert this BasicString to narrow (char, UTF-8) representation
        [[nodiscard]] BasicString<char> ToNarrow() const noexcept {
            return BasicString<char>::FromWide(this->CStr());
        }

        // Convert this BasicString to wide (wchar_t, UTF-16/32) representation
        [[nodiscard]] BasicString<wchar_t> ToWide() const noexcept {
            return BasicString<wchar_t>::FromNarrow(this->CStr());
        }

        // =========================================================================
        // CONCATENATION OPERATORS
        // =========================================================================

        BasicString& operator+=(const CharT* str) noexcept {
            Append(str);
            return *this;
        }

        BasicString& operator+=(const BasicString& other) noexcept {
            Append(other);
            return *this;
        }

        BasicString& operator+=(CharT c) noexcept {
            CharT str[2] = {c, CharT{0}};
            Append(str);
            return *this;
        }

        [[nodiscard]] friend BasicString operator+(const BasicString& lhs, const BasicString& rhs) noexcept {
            BasicString result;
            result.Reserve(lhs.mLength + rhs.mLength);
            result.Append(lhs);
            result.Append(rhs);
            return result;
        }

        [[nodiscard]] friend BasicString operator+(const BasicString& lhs, const CharT* rhs) noexcept {
            BasicString result;
            SizeType rhsLen = rhs ? StrLen(rhs) : 0;
            result.Reserve(lhs.mLength + rhsLen);
            result.Append(lhs);
            if (rhs) result.Append(rhs);
            return result;
        }

        [[nodiscard]] friend BasicString operator+(const CharT* lhs, const BasicString& rhs) noexcept {
            BasicString result;
            SizeType lhsLen = lhs ? StrLen(lhs) : 0;
            result.Reserve(lhsLen + rhs.mLength);
            if (lhs) result.Append(lhs);
            result.Append(rhs);
            return result;
        }

        [[nodiscard]] friend BasicString operator+(const BasicString& lhs, CharT rhs) noexcept {
            BasicString result;
            result.Reserve(lhs.mLength + 1);
            result.Append(lhs);
            result.Append(CharT{rhs});
            return result;
        }

        [[nodiscard]] friend BasicString operator+(CharT lhs, const BasicString& rhs) noexcept {
            BasicString result;
            result.Reserve(1 + rhs.mLength);
            CharT str[2] = {lhs, CharT{0}};
            result.Append(str);
            result.Append(rhs);
            return result;
        }

        // =========================================================================
        // COMPARISON OPERATIONS
        // =========================================================================

        [[nodiscard]] bool operator==(const BasicString& other) const noexcept {
            if (mLength != other.mLength) return false;
            return StrCompare(GetData(), other.GetData(), mLength) == 0;
        }

        [[nodiscard]] bool operator!=(const BasicString& other) const noexcept {
            return !(*this == other);
        }

        [[nodiscard]] int Compare(const BasicString& other) const noexcept {
            SizeType minLen = (mLength < other.mLength) ? mLength : other.mLength;
            int result = StrCompare(GetData(), other.GetData(), minLen);

            if (result == 0) {
                if (mLength < other.mLength) return -1;
                if (mLength > other.mLength) return 1;
            }
            return result;
        }

        [[nodiscard]] int CompareIgnoreCase(const BasicString& other) const noexcept {
            SizeType minLen = (mLength < other.mLength) ? mLength : other.mLength;

            const CharT* data1 = GetData();
            const CharT* data2 = other.GetData();

            for (SizeType i = 0; i < minLen; ++i) {
                CharT c1 = ToLowerChar(data1[i]);
                CharT c2 = ToLowerChar(data2[i]);
                if (c1 != c2) return (c1 < c2) ? -1 : 1;
            }

            if (mLength < other.mLength) return -1;
            if (mLength > other.mLength) return 1;
            return 0;
        }

        // =========================================================================
        // STRING FORMATTING
        // =========================================================================

        // Format a string using placeholder syntax: Format("Hello {0}, you have {1} HP", name, hp)
        // Placeholders are {0}, {1}, {2}, ... referencing arguments by index.
        // Supported argument types: integral, floating point, bool, char*, BasicString, pointers.
        // Unknown/unsupported types produce "{?}".
        //
        // Example:
        //   auto s = String::Format("Player {0} scored {1} points", playerName, score);
        template<typename... Args>
        [[nodiscard]] static BasicString Format(const CharT* fmt, Args&&... args) noexcept {
            if (!fmt) return BasicString();

            // Convert each argument to a BasicString up front
            constexpr SizeType ArgCount = sizeof...(Args);
            BasicString argStrings[ArgCount > 0 ? ArgCount : 1];
            [[maybe_unused]] SizeType argIdx = 0;
            (FormatArg(argStrings, argIdx, std::forward<Args>(args)), ...);

            BasicString result;
            const CharT* p = fmt;

            while (*p != CharT{0}) {
                // Opening brace?
                if (*p == GetOpenBrace()) {
                    ++p;

                    // Escaped brace: {{ -> {
                    if (*p == GetOpenBrace()) {
                        result += GetOpenBrace();
                        ++p;
                        continue;
                    }

                    // Parse placeholder index
                    SizeType index = 0;
                    bool hasDigit = false;
                    while (*p != CharT{0} && IsDigit(*p)) {
                        index = index * 10 + static_cast<SizeType>(*p - GetDigitChar(0));
                        ++p;
                        hasDigit = true;
                    }

                    // Expect closing brace
                    if (*p == GetCloseBrace()) {
                        ++p;
                        if (hasDigit && index < ArgCount) {
                            result.Append(argStrings[index]);
                        } else {
                            // Invalid placeholder — emit literal "{?}"
                            result += GetOpenBrace();
                            result += GetQuestionChar();
                            result += GetCloseBrace();
                        }
                    } else {
                        // Malformed — emit '{' and back up to reparse
                        result += GetOpenBrace();
                    }
                }
                // Closing brace escape: }} -> }
                else if (*p == GetCloseBrace() && *(p + 1) == GetCloseBrace()) {
                    result += GetCloseBrace();
                    p += 2;
                }
                else {
                    result += *p;
                    ++p;
                }
            }

            return result;
        }

        // Convenience overload accepting a BasicString as the format string
        template<typename... Args>
        [[nodiscard]] static BasicString Format(const BasicString& fmt, Args&&... args) noexcept {
            return Format(fmt.CStr(), std::forward<Args>(args)...);
        }

    private:
        // -------------------------------------------------------------------------
        // Format helpers
        // -------------------------------------------------------------------------

        static constexpr CharT GetOpenBrace() noexcept {
            return std::is_same_v<CharT, char> ? '{' : L'{';
        }

        static constexpr CharT GetCloseBrace() noexcept {
            return std::is_same_v<CharT, char> ? '}' : L'}';
        }

        static constexpr CharT GetQuestionChar() noexcept {
            return std::is_same_v<CharT, char> ? '?' : L'?';
        }

        // Convert a single argument to BasicString and store it at argStrings[idx]
        template<typename T>
        static void FormatArg(BasicString* argStrings, SizeType& idx, T&& val) noexcept {
            using DecayT = std::decay_t<T>;

            if constexpr (std::is_same_v<DecayT, BasicString>) {
                argStrings[idx++] = val;
            }
            // String literals: T deduces as CharT(&)[N] or const CharT(&)[N]
            else if constexpr (std::is_array_v<std::remove_reference_t<T>> &&
                               std::is_same_v<std::remove_cv_t<std::remove_extent_t<std::remove_reference_t<T>>>, CharT>) {
                argStrings[idx++] = BasicString(val);
            }
            else if constexpr ((std::is_same_v<DecayT, const CharT*> || std::is_same_v<DecayT, CharT*>) &&
                               !std::is_array_v<std::remove_reference_t<T>>) {
                argStrings[idx++] = val ? BasicString(val) : BasicString();
            }
            else if constexpr (std::is_same_v<DecayT, bool>) {
                argStrings[idx++] = FromBool(val);
            }
            else if constexpr (std::is_integral_v<DecayT>) {
                argStrings[idx++] = FromInt(val);
            }
            else if constexpr (std::is_floating_point_v<DecayT>) {
                argStrings[idx++] = FromFloat(val);
            }
            else if constexpr (std::is_pointer_v<DecayT>) {
                argStrings[idx++] = FromPointer(val);
            }
            else {
                // Unsupported type — store "{?}" so the placeholder stays visible
                CharT buf[4] = { GetOpenBrace(), GetQuestionChar(), GetCloseBrace(), CharT{0} };
                argStrings[idx++] = BasicString(buf);
            }
        }

    public:
    };

    // =============================================================================
    // TYPE ALIASES FOR CONVENIENCE
    // =============================================================================

    using String = BasicString<char, DefaultAllocator<char>>;
    using StringW = BasicString<wchar_t, DefaultAllocator<wchar_t>>;

    // =============================================================================
    // FREE FUNCTION WRAPPERS FOR Format
    // =============================================================================

    // Plu::Format("Hello {0}!", name)  — delegates to BasicString<char>::Format
    template<typename... Args>
    [[nodiscard]] inline String Format(const char* fmt, Args&&... args) noexcept {
        return String::Format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    [[nodiscard]] inline String Format(const String& fmt, Args&&... args) noexcept {
        return String::Format(fmt, std::forward<Args>(args)...);
    }

    // Plu::FormatW — wide-string variant
    template<typename... Args>
    [[nodiscard]] inline StringW FormatW(const wchar_t* fmt, Args&&... args) noexcept {
        return StringW::Format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    [[nodiscard]] inline StringW FormatW(const StringW& fmt, Args&&... args) noexcept {
        return StringW::Format(fmt, std::forward<Args>(args)...);
    }

}

// The hash specializations for BasicString / BasicPath live in Hashers/String.h, which
// forward-declares both types. Including it here — after the definitions — is what
// guarantees that any translation unit able to name the type can also key a HashMap or
// HashSet with it. Without this, such a TU would silently fall back to DefaultHash's
// byte-wise path and hash the string object rather than its characters.
#include "Hashers/String.h"
