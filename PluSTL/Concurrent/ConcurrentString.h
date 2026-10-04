//
// Created by Plutex on 2026-08-06.
//

#ifndef PLUSTL_CONCURRENTSTRING_H
#define PLUSTL_CONCURRENTSTRING_H

#include <cstddef>

#include "Array/Array.h"
#include "String/String.h"
#include "Concurrent/Detail/SharedGuarded.h"

namespace Plu
{
    // ========================================================================
    // ConcurrentString — a guarded String, not a new string type
    // ========================================================================
    //
    // BasicString has SSO but no copy-on-write and no refcount, so a *copy* of a
    // String is already thread-safe: two threads holding two Strings share nothing.
    // The only hazard is one instance shared and mutated in place — and the sharp
    // edge there is CStr()/operator[]/Data(), which hand out a raw pointer into a
    // buffer another thread may reallocate on the very next Append.
    //
    // So this is String's API over a shared_mutex (Detail::SharedGuarded does the
    // locking), with the handle-returning members removed: **CStr(), Data(),
    // operator[] and iterators deliberately do not exist**, because there is no way
    // to publish a pointer into the buffer and still call the result safe. Every
    // read returns a copy — Substring, Split and ToUpper/ToLower give back plain
    // Strings — and anything more involved goes through Read()/Write(), which run a
    // callback while the lock is held.
    //
    // Use it for a genuinely shared mutable text buffer — the editor console
    // accumulator, a log sink several threads append to. For everything else pass a
    // plain String by value: it is cheaper and has no locking at all.
    class ConcurrentString : private Detail::SharedGuarded<String>
    {
        using Base = Detail::SharedGuarded<String>;

    public:
        using ValueType = String::ValueType;
        using SizeType = String::SizeType;

        static constexpr SizeType Npos = String::Npos;

        ConcurrentString() = default;

        explicit ConcurrentString(const String& value) : Base(value) {}
        explicit ConcurrentString(String&& value) : Base(static_cast<String&&>(value)) {}
        explicit ConcurrentString(const char* value) : Base(String(value)) {}

        // ====================================================================
        // VISITORS
        // ====================================================================
        // fn(const String&) under a shared lock (several readers at once) / fn(String&)
        // under an exclusive one. Both return whatever the callback returns, by value.
        // The reference is valid only for the duration of the call: do not store it, and
        // do not stash CStr() from it either. Must not re-enter this object — the lock is
        // not recursive.
        using Base::Read;
        using Base::Write;

        // A copy of the contents — the closest thing to "give me the String".
        using Base::Get;

        // ====================================================================
        // READS — every one of them returns a copy or a plain value
        // ====================================================================

        [[nodiscard]] SizeType Length() const { return Read([](const String& v) { return v.Length(); }); }
        [[nodiscard]] SizeType Capacity() const { return Read([](const String& v) { return v.Capacity(); }); }
        [[nodiscard]] bool IsEmpty() const { return Read([](const String& v) { return v.IsEmpty(); }); }

        [[nodiscard]] SizeType Find(char c, SizeType startPos = 0) const
        {
            return Read([c, startPos](const String& v) { return v.Find(c, startPos); });
        }

        [[nodiscard]] SizeType Find(const char* substring, SizeType startPos = 0) const
        {
            return Read([substring, startPos](const String& v) { return v.Find(substring, startPos); });
        }

        [[nodiscard]] SizeType RFind(char c, SizeType startPos = Npos) const
        {
            return Read([c, startPos](const String& v) { return v.RFind(c, startPos); });
        }

        [[nodiscard]] bool Contains(const char* substring) const
        {
            return Read([substring](const String& v) { return v.Contains(substring); });
        }

        [[nodiscard]] bool Contains(const String& substring) const { return Contains(substring.CStr()); }

        [[nodiscard]] bool StartsWith(const char* prefix) const
        {
            return Read([prefix](const String& v) { return v.StartsWith(prefix); });
        }

        [[nodiscard]] bool StartsWith(const String& prefix) const { return StartsWith(prefix.CStr()); }

        [[nodiscard]] bool EndsWith(const char* suffix) const
        {
            return Read([suffix](const String& v) { return v.EndsWith(suffix); });
        }

        [[nodiscard]] bool EndsWith(const String& suffix) const { return EndsWith(suffix.CStr()); }

        [[nodiscard]] String Substring(SizeType start, SizeType length = Npos) const
        {
            return Read([start, length](const String& v) { return v.Substring(start, length); });
        }

        [[nodiscard]] DynamicArray<String> Split(char delimiter) const
        {
            return Read([delimiter](const String& v) { return v.Split(delimiter); });
        }

        [[nodiscard]] DynamicArray<String> Split(const char* delimiter) const
        {
            return Read([delimiter](const String& v) { return v.Split(delimiter); });
        }

        [[nodiscard]] String ToUpper() const { return Read([](const String& v) { return v.ToUpper(); }); }
        [[nodiscard]] String ToLower() const { return Read([](const String& v) { return v.ToLower(); }); }

        [[nodiscard]] String Strip(const char* chars = nullptr) const
        {
            return Read([chars](const String& v) { return v.Strip(chars); });
        }

        [[nodiscard]] String StripLeft(const char* chars = nullptr) const
        {
            return Read([chars](const String& v) { return v.StripLeft(chars); });
        }

        [[nodiscard]] String StripRight(const char* chars = nullptr) const
        {
            return Read([chars](const String& v) { return v.StripRight(chars); });
        }

        [[nodiscard]] int Compare(const String& other) const
        {
            return Read([&other](const String& v) { return v.Compare(other); });
        }

        [[nodiscard]] bool Equals(const String& other) const
        {
            return Read([&other](const String& v) { return v == other; });
        }

        [[nodiscard]] bool Equals(const char* other) const
        {
            return Read([other](const String& v) { return v == other; });
        }

        [[nodiscard]] bool operator==(const String& other) const { return Equals(other); }
        [[nodiscard]] bool operator!=(const String& other) const { return !Equals(other); }

        // ====================================================================
        // WRITES
        // ====================================================================

        using Base::Assign;

        void Assign(const char* value) { Write([value](String& v) { v = value; }); }

        ConcurrentString& operator=(const String& value) { Assign(value); return *this; }
        ConcurrentString& operator=(String&& value) { Assign(static_cast<String&&>(value)); return *this; }
        ConcurrentString& operator=(const char* value) { Assign(value); return *this; }

        void Append(const String& value) { Write([&value](String& v) { v.Append(value); }); }
        void Append(const char* value) { Write([value](String& v) { v.Append(value); }); }

        ConcurrentString& operator+=(const String& value) { Append(value); return *this; }
        ConcurrentString& operator+=(const char* value) { Append(value); return *this; }

        void Insert(SizeType pos, const char* value) { Write([pos, value](String& v) { v.Insert(pos, value); }); }
        void Insert(SizeType pos, const String& value) { Insert(pos, value.CStr()); }

        // Erases `length` characters from `start` — String::Remove, not a search-and-delete.
        void Remove(SizeType start, SizeType length = Npos)
        {
            Write([start, length](String& v) { v.Remove(start, length); });
        }

        // Forwards to String::Replace — first occurrence only, not all of them.
        void Replace(const char* oldSubstring, const char* newSubstring)
        {
            Write([oldSubstring, newSubstring](String& v) { v.Replace(oldSubstring, newSubstring); });
        }

        void ReplaceAt(SizeType index, char newChar) { Write([index, newChar](String& v) { v.ReplaceAt(index, newChar); }); }

        void ToUpperInPlace() { Write([](String& v) { v.ToUpperInPlace(); }); }
        void ToLowerInPlace() { Write([](String& v) { v.ToLowerInPlace(); }); }

        void StripInPlace(const char* chars = nullptr) { Write([chars](String& v) { v.StripInPlace(chars); }); }
        void StripLeftInPlace(const char* chars = nullptr) { Write([chars](String& v) { v.StripLeftInPlace(chars); }); }
        void StripRightInPlace(const char* chars = nullptr) { Write([chars](String& v) { v.StripRightInPlace(chars); }); }

        void Clear() { Write([](String& v) { v.Clear(); }); }

        // Grows only, exactly like String::Reserve.
        void Reserve(SizeType capacity) { Write([capacity](String& v) { v.Reserve(capacity); }); }

        // Empties the buffer and returns what it held, in one critical section. The
        // "flush the accumulated log and start over" primitive — nothing appended by
        // another thread can slip through between the read and the clear.
        using Base::Take;
    };
}

#endif //PLUSTL_CONCURRENTSTRING_H
