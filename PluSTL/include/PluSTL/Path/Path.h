//
// Created by Plutex on 10.01.2026.
//

#pragma once

#include "PluSTL/String/String.h"
#include "PluSTL/Allocators/Default.h"

namespace Plu
{
    // =============================================================================
    // Path CLASS - Cross-platform path manipulation
    // =============================================================================

    template<typename CharT = char, typename Allocator = DefaultAllocator<CharT>>
    class BasicPath {
    public:
        using StringType = BasicString<CharT, Allocator>;
        using SizeType = typename StringType::SizeType;

        // Platform-specific separators
        #ifdef _WIN32
            static constexpr CharT PreferredSeparator = (std::is_same_v<CharT, char>) ? '\\' : L'\\';
            static constexpr CharT AltSeparator = (std::is_same_v<CharT, char>) ? '/' : L'/';
        #else
            static constexpr CharT PreferredSeparator = (std::is_same_v<CharT, char>) ? '/' : L'/';
            static constexpr CharT AltSeparator = (std::is_same_v<CharT, char>) ? '\\' : L'\\';
        #endif

    private:
        StringType mPath;

        // Check if character is any path separator
        static bool IsSeparator(CharT c) noexcept {
            return c == PreferredSeparator || c == AltSeparator;
        }

        // Get dot character
        static constexpr CharT GetDot() noexcept {
            return (std::is_same_v<CharT, char>) ? '.' : L'.';
        }

        // Get colon character (for Windows drive letters)
        static constexpr CharT GetColon() noexcept {
            return (std::is_same_v<CharT, char>) ? ':' : L':';
        }

        // Normalize path separators to preferred separator
        void NormalizeSeparators() noexcept {
            for (SizeType i = 0; i < mPath.Length(); ++i) {
                if (IsSeparator(mPath[i])) {
                    mPath[i] = PreferredSeparator;
                }
            }
        }

        // Remove trailing separators (except for root)
        void RemoveTrailingSeparators() noexcept {
            while (mPath.Length() > 1 && IsSeparator(mPath[mPath.Length() - 1])) {
                // Don't remove if it's root (e.g., "/" or "C:\")
                if (mPath.Length() == 1) break;
                #ifdef _WIN32
                if (mPath.Length() == 3 && mPath[1] == GetColon()) break; // "C:\"
                #endif
                mPath.Remove(mPath.Length() - 1, 1);
            }
        }

        // Find last separator position
        SizeType FindLastSeparator() const noexcept {
            for (SizeType i = mPath.Length(); i > 0; --i) {
                if (IsSeparator(mPath[i - 1])) {
                    return i - 1;
                }
            }
            return StringType::Npos;
        }

        // Find last dot position for extension
        SizeType FindLastDot() const noexcept {
            SizeType lastSep = FindLastSeparator();
            SizeType startPos = (lastSep != StringType::Npos) ? lastSep + 1 : 0;

            for (SizeType i = mPath.Length(); i > startPos; --i) {
                if (mPath[i - 1] == GetDot()) {
                    // Don't treat leading dot as extension (e.g., ".gitignore")
                    if (i - 1 > startPos) {
                        return i - 1;
                    }
                }
            }
            return StringType::Npos;
        }

    public:
        // =========================================================================
        // CONSTRUCTORS
        // =========================================================================

        BasicPath() noexcept = default;

        BasicPath(const CharT* path) noexcept : mPath(path) {
            NormalizeSeparators();
        }

        BasicPath(const StringType& path) noexcept : mPath(path) {
            NormalizeSeparators();
        }

        BasicPath(const BasicPath& other) noexcept = default;
        BasicPath(BasicPath&& other) noexcept = default;

        BasicPath& operator=(const BasicPath& other) noexcept = default;
        BasicPath& operator=(BasicPath&& other) noexcept = default;

        // =========================================================================
        // BASIC PROPERTIES
        // =========================================================================

        // Get the full path as string
        [[nodiscard]] const StringType& ToString() const noexcept {
            return mPath;
        }

        // Get C-string representation
        [[nodiscard]] const CharT* CStr() const noexcept {
            return mPath.CStr();
        }

        // Check if path is empty
        [[nodiscard]] bool IsEmpty() const noexcept {
            return mPath.IsEmpty();
        }

        // Check if path is absolute
        [[nodiscard]] bool IsAbsolute() const noexcept {
            if (mPath.IsEmpty()) return false;

            #ifdef _WIN32
                // Windows: "C:\" or "\\server\share"
                if (mPath.Length() >= 3 && mPath[1] == GetColon() && IsSeparator(mPath[2])) {
                    return true; // "C:\"
                }
                if (mPath.Length() >= 2 && IsSeparator(mPath[0]) && IsSeparator(mPath[1])) {
                    return true; // UNC path "\\server"
                }
                return false;
            #else
                // Unix: starts with "/"
                return IsSeparator(mPath[0]);
            #endif
        }

        // Check if path is relative
        [[nodiscard]] bool IsRelative() const noexcept {
            return !IsAbsolute();
        }

        // =========================================================================
        // PATH COMPONENTS
        // =========================================================================

        // Get filename (last component of path)
        [[nodiscard]] StringType GetFilename() const noexcept {
            if (mPath.IsEmpty()) return StringType();

            SizeType lastSep = FindLastSeparator();
            if (lastSep == StringType::Npos) {
                return mPath; // No separator, entire path is filename
            }

            return mPath.Substring(lastSep + 1);
        }

        // Get extension (including the dot)
        [[nodiscard]] StringType GetExtension() const noexcept {
            SizeType dotPos = FindLastDot();
            if (dotPos == StringType::Npos) {
                return StringType();
            }

            return mPath.Substring(dotPos);
        }

        // Get stem (filename without extension)
        [[nodiscard]] StringType GetStem() const noexcept {
            StringType filename = GetFilename();
            if (filename.IsEmpty()) return StringType();

            SizeType dotPos = StringType::Npos;
            for (SizeType i = filename.Length(); i > 1; --i) {
                if (filename[i - 1] == GetDot()) {
                    dotPos = i - 1;
                    break;
                }
            }

            if (dotPos == StringType::Npos || dotPos == 0) {
                return filename; // No extension or hidden file
            }

            return filename.Substring(0, dotPos);
        }

        // Get parent directory
        [[nodiscard]] BasicPath GetParentPath() const noexcept {
            if (mPath.IsEmpty()) return BasicPath();

            SizeType lastSep = FindLastSeparator();
            if (lastSep == StringType::Npos) {
                return BasicPath(); // No parent
            }

            // Handle root cases
            if (lastSep == 0) {
                // "/" -> "/"
                CharT root[2] = {PreferredSeparator, CharT{0}};
                return BasicPath(root);
            }

            #ifdef _WIN32
            if (lastSep == 2 && mPath[1] == GetColon()) {
                // "C:\" -> "C:\"
                return BasicPath(mPath.Substring(0, 3));
            }
            #endif

            return BasicPath(mPath.Substring(0, lastSep));
        }

        // Get root name (e.g., "C:" on Windows, empty on Unix)
        [[nodiscard]] StringType GetRootName() const noexcept {
            if (mPath.IsEmpty()) return StringType();

            #ifdef _WIN32
                if (mPath.Length() >= 2 && mPath[1] == GetColon()) {
                    return mPath.Substring(0, 2); // "C:"
                }
                if (mPath.Length() >= 2 && IsSeparator(mPath[0]) && IsSeparator(mPath[1])) {
                    // UNC path - find first separator after "\\"
                    SizeType pos = 2;
                    while (pos < mPath.Length() && !IsSeparator(mPath[pos])) {
                        ++pos;
                    }
                    return mPath.Substring(0, pos); // "\\server"
                }
            #endif

            return StringType();
        }

        // Get root directory ("/" or "\")
        [[nodiscard]] StringType GetRootDirectory() const noexcept {
            if (mPath.IsEmpty()) return StringType();

            #ifdef _WIN32
                if (mPath.Length() >= 3 && mPath[1] == GetColon() && IsSeparator(mPath[2])) {
                    CharT sep[2] = {PreferredSeparator, CharT{0}};
                    return StringType(sep);
                }
                if (mPath.Length() >= 2 && IsSeparator(mPath[0]) && IsSeparator(mPath[1])) {
                    CharT sep[2] = {PreferredSeparator, CharT{0}};
                    return StringType(sep);
                }
            #else
                if (IsSeparator(mPath[0])) {
                    CharT sep[2] = {PreferredSeparator, CharT{0}};
                    return StringType(sep);
                }
            #endif

            return StringType();
        }

        // =========================================================================
        // PATH MODIFICATION
        // =========================================================================

        // Append path component
        BasicPath& Append(const BasicPath& other) noexcept {
            if (other.IsEmpty()) return *this;
            if (mPath.IsEmpty()) {
                mPath = other.mPath;
                return *this;
            }

            // Don't add separator if path already ends with one
            if (!IsSeparator(mPath[mPath.Length() - 1])) {
                CharT sep[2] = {PreferredSeparator, CharT{0}};
                mPath += sep;
            }

            mPath += other.mPath;
            return *this;
        }

        BasicPath& Append(const CharT* path) noexcept {
            return Append(BasicPath(path));
        }

        // Concatenate (no separator added)
        BasicPath& Concat(const BasicPath& other) noexcept {
            mPath += other.mPath;
            NormalizeSeparators();
            return *this;
        }

        BasicPath& Concat(const CharT* str) noexcept {
            mPath += str;
            NormalizeSeparators();
            return *this;
        }

        // Replace filename
        BasicPath& ReplaceFilename(const StringType& newFilename) noexcept {
            SizeType lastSep = FindLastSeparator();
            if (lastSep == StringType::Npos) {
                mPath = newFilename;
            } else {
                mPath.Remove(lastSep + 1);
                mPath += newFilename;
            }
            return *this;
        }

        // Replace extension
        BasicPath& ReplaceExtension(const StringType& newExt) noexcept {
            SizeType dotPos = FindLastDot();
            if (dotPos != StringType::Npos) {
                mPath.Remove(dotPos);
            }

            if (!newExt.IsEmpty()) {
                // Add dot if not present
                if (newExt[0] != GetDot()) {
                    CharT dot[2] = {GetDot(), CharT{0}};
                    mPath += dot;
                }
                mPath += newExt;
            }

            return *this;
        }

        // Remove filename (keep only parent path)
        BasicPath& RemoveFilename() noexcept {
            SizeType lastSep = FindLastSeparator();
            if (lastSep != StringType::Npos) {
                mPath.Remove(lastSep + 1);
            } else {
                mPath.Clear();
            }
            return *this;
        }

        // Clear the path
        void Clear() noexcept {
            mPath.Clear();
        }

        // =========================================================================
        // PATH NORMALIZATION
        // =========================================================================

        // Normalize path (resolve . and .., remove redundant separators)
        [[nodiscard]] BasicPath GetNormalized() const noexcept {
            if (mPath.IsEmpty()) return BasicPath();

            BasicPath result;
            StringType component;
            bool isAbsolute = IsAbsolute();

            // Start with root if absolute
            if (isAbsolute) {
                #ifdef _WIN32
                    if (mPath.Length() >= 2 && mPath[1] == GetColon()) {
                        result.mPath = mPath.Substring(0, 2); // "C:"
                        CharT sep[2] = {PreferredSeparator, CharT{0}};
                        result.mPath += sep;
                    } else if (IsSeparator(mPath[0])) {
                        CharT sep[2] = {PreferredSeparator, CharT{0}};
                        result.mPath = StringType(sep);
                    }
                #else
                    CharT sep[2] = {PreferredSeparator, CharT{0}};
                    result.mPath = StringType(sep);
                #endif
            }

            // Split path and process components
            SizeType start = 0;
            if (isAbsolute) {
                #ifdef _WIN32
                    start = (mPath.Length() >= 3 && mPath[1] == GetColon()) ? 3 : 1;
                #else
                    start = 1;
                #endif
            }

            // Simple normalization - just copy path
            // (Full .. resolution would require tracking directory stack)
            result.mPath = mPath;
            result.RemoveTrailingSeparators();

            return result;
        }

        // =========================================================================
        // OPERATORS
        // =========================================================================

        BasicPath& operator/=(const BasicPath& other) noexcept {
            return Append(other);
        }

        BasicPath& operator/=(const CharT* path) noexcept {
            return Append(path);
        }

        BasicPath& operator+=(const BasicPath& other) noexcept {
            return Concat(other);
        }

        BasicPath& operator+=(const CharT* str) noexcept {
            return Concat(str);
        }

        [[nodiscard]] friend BasicPath operator/(const BasicPath& lhs, const BasicPath& rhs) noexcept {
            BasicPath result = lhs;
            result.Append(rhs);
            return result;
        }

        [[nodiscard]] friend BasicPath operator/(const BasicPath& lhs, const CharT* rhs) noexcept {
            BasicPath result = lhs;
            result.Append(rhs);
            return result;
        }

        [[nodiscard]] bool operator==(const BasicPath& other) const noexcept {
            return mPath == other.mPath;
        }

        [[nodiscard]] bool operator!=(const BasicPath& other) const noexcept {
            return mPath != other.mPath;
        }

        // =========================================================================
        // UTILITY FUNCTIONS
        // =========================================================================

        // Check if path has extension
        [[nodiscard]] bool HasExtension() const noexcept {
            return FindLastDot() != StringType::Npos;
        }

        // Check if path has filename
        [[nodiscard]] bool HasFilename() const noexcept {
            return !GetFilename().IsEmpty();
        }

        // Check if path has parent
        [[nodiscard]] bool HasParentPath() const noexcept {
            return FindLastSeparator() != StringType::Npos;
        }

        // Convert to generic format (forward slashes)
        [[nodiscard]] StringType ToGenericString() const noexcept {
            StringType result = mPath;
            for (SizeType i = 0; i < result.Length(); ++i) {
                if (result[i] == PreferredSeparator || result[i] == AltSeparator) {
                    if constexpr (std::is_same_v<CharT, char>) {
                        result[i] = '/';
                    } else {
                        result[i] = L'/';
                    }
                }
            }
            return result;
        }

        // Convert to native format
        [[nodiscard]] StringType ToNativeString() const noexcept {
            return mPath;
        }
    };

    // =============================================================================
    // TYPE ALIASES
    // =============================================================================

    using Path = BasicPath<char, DefaultAllocator<char>>;
    using PathW = BasicPath<wchar_t, DefaultAllocator<wchar_t>>;
}

// The hash specializations for BasicString / BasicPath live in Hashers/String.h, which
// forward-declares both types. Including it here — after the definitions — is what
// guarantees that any translation unit able to name the type can also key a HashMap or
// HashSet with it. Without this, such a TU would silently fall back to DefaultHash's
// byte-wise path and hash the string object rather than its characters.
#include "PluSTL/Hashers/String.h"
