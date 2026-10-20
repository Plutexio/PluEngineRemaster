#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

// ============================================================================
// PluSTL allocators
// ============================================================================
//
// The allocator concept every PluSTL container expects:
//
//   using ValueType = T;
//   template<typename U> using Rebind = <this allocator, for U>;   // optional
//   T*   Allocate(std::size_t count) noexcept;                     // nullptr on failure
//   void Deallocate(T* ptr, std::size_t count) noexcept;
//   void Construct(T* ptr, Args&&... args);
//   void Destroy(T* ptr) noexcept;
//
// Allocate is failable, not throwing: it returns nullptr and every container
// checks the result before touching it. Containers that need memory for a type
// other than T (node-based maps, slot tables) go through RebindAllocatorT so a
// caller-supplied allocator is actually used instead of being silently dropped.

namespace Plu
{
    // ========================================================================
    // DEFAULT ALLOCATOR
    // ========================================================================
    // Plain global new/delete, with two things the raw operators do not give you
    // for free: over-aligned types land on a correctly aligned block, and a
    // count * sizeof(T) overflow fails instead of wrapping into a small request.
    template<typename T>
    class DefaultAllocator
    {
    public:
        using ValueType = T;

        template<typename U>
        using Rebind = DefaultAllocator<U>;

        DefaultAllocator() noexcept = default;

        template<typename U>
        explicit DefaultAllocator(const DefaultAllocator<U>&) noexcept {}

        [[nodiscard]] T* Allocate(std::size_t count) noexcept
        {
            if (count == 0) return nullptr;
            // Reject a request whose byte size would wrap; the caller sees the same
            // nullptr it sees for a genuine out-of-memory.
            if (count > SIZE_MAX / sizeof(T)) return nullptr;

            const std::size_t bytes = count * sizeof(T);
            if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            {
                return static_cast<T*>(::operator new(bytes, std::align_val_t{alignof(T)}, std::nothrow));
            }
            else
            {
                return static_cast<T*>(::operator new(bytes, std::nothrow));
            }
        }

        void Deallocate(T* ptr, std::size_t count) noexcept
        {
            if (!ptr) return;

            const std::size_t bytes = count * sizeof(T);
            if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            {
                ::operator delete(ptr, bytes, std::align_val_t{alignof(T)});
            }
            else
            {
                ::operator delete(ptr, bytes);
            }
        }

        template<typename... Args>
        void Construct(T* ptr, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        {
            new (static_cast<void*>(ptr)) T(std::forward<Args>(args)...);
        }

        void Destroy(T* ptr) noexcept
        {
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                ptr->~T();
            }
        }

        // Stateless: any two instances can free each other's memory.
        bool operator==(const DefaultAllocator&) const noexcept { return true; }
        bool operator!=(const DefaultAllocator&) const noexcept { return false; }
    };

    // ========================================================================
    // REBINDING
    // ========================================================================
    // RebindAllocatorT<Allocator, U> is `Allocator` re-targeted at U. An allocator
    // that declares a Rebind alias gets its own type back; one that does not falls
    // back to DefaultAllocator<U>, which is what the pre-rebind containers
    // hardcoded anyway — so the fallback never makes an existing type worse.
    namespace Detail
    {
        template<typename Allocator, typename U, typename = void>
        struct RebindAllocator
        {
            using Type = DefaultAllocator<U>;
        };

        template<typename Allocator, typename U>
        struct RebindAllocator<Allocator, U, std::void_t<typename Allocator::template Rebind<U>>>
        {
            using Type = typename Allocator::template Rebind<U>;
        };
    }

    template<typename Allocator, typename U>
    using RebindAllocatorT = typename Detail::RebindAllocator<Allocator, U>::Type;
}

// Transitional: DefaultAllocator used to live in the global namespace, and it appears
// as a default template argument across the engine. The using-declaration keeps that
// spelling working while call sites move to Plu::DefaultAllocator.
using Plu::DefaultAllocator;
