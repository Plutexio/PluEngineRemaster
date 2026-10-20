#pragma once

#include <type_traits>
#include <utility>
#include "PluSTL/Pointers/ControlBlock.h"

namespace Plu
{
    template<typename T>
    class TOwningPointer;

    template<typename T>
    struct ControlBlock;

    template<typename T>
    class TUsePointer;
    
    // Funkcje pomocnicze do konwersji
    
    // Upcasting (Derived -> Base) - zawsze bezpieczny
    template<typename T>
    TUsePointer<T> CastFromOwning(const TOwningPointer<T>& owning)
    {
        return TUsePointer<T>(owning);
    }

    template<typename T, typename U>
    TUsePointer<T> CastFromOwning(const TOwningPointer<U>& owning)
    {
        static_assert(std::is_base_of<T, U>::value || std::is_same<T, U>::value || std::is_base_of<U, T>::value, 
            "Types must be related through inheritance or be the same type");
        return TUsePointer<T>(owning);
    }

    // Dynamic cast dla TOwningPointer (Base -> Derived)
    template<typename To, typename From>
    TOwningPointer<To> DynamicCast(const TOwningPointer<From>& from)
    {
        if (!from)
            return nullptr;

        To* casted = dynamic_cast<To*>(from.GetRaw());
        if (!casted)
            return nullptr;

        // Tworzymy nowy TOwningPointer współdzielący control block
        TOwningPointer<To> result;
        result.control = reinterpret_cast<ControlBlock<To>*>(from.control);
        if (result.control)
        {
            PLU_PTR_ASSERT_OWNER(result.control);
            result.control->strongCount.fetch_add(1, std::memory_order_relaxed);
        }
        return result;
    }

    // Dynamic cast dla TUsePointer (Base -> Derived)
    template<typename To, typename From>
    TUsePointer<To> DynamicCast(const TUsePointer<From>& from)
    {
        if (!from.IsValid())
            return nullptr;

        To* casted = dynamic_cast<To*>(from.GetRaw());
        if (!casted)
            return nullptr;

        // Tworzymy nowy TUsePointer współdzielący control block
        TUsePointer<To> result;
        result.control = reinterpret_cast<ControlBlock<To>*>(from.control);
        if (result.control)
        {
            result.control->weakCount.fetch_add(1, std::memory_order_relaxed);
        }
        return result;
    }

    // Static cast (szybszy ale niebezpieczny - bez sprawdzania)
    template<typename To, typename From>
    TOwningPointer<To> StaticCast(const TOwningPointer<From>& from)
    {
        TOwningPointer<To> result;
        result.control = reinterpret_cast<ControlBlock<To>*>(from.control);
        if (result.control)
        {
            PLU_PTR_ASSERT_OWNER(result.control);
            result.control->strongCount.fetch_add(1, std::memory_order_relaxed);
        }
        return result;
    }

    template<typename To, typename From>
    TUsePointer<To> StaticCast(const TUsePointer<From>& from)
    {
        TUsePointer<To> result;
        result.control = reinterpret_cast<ControlBlock<To>*>(from.control);
        if (result.control)
        {
            result.control->weakCount.fetch_add(1, std::memory_order_relaxed);
        }
        return result;
    }

    template<typename T, typename... Args>
    TOwningPointer<T> CreateOwning(Args&&... args)
    {
        return TOwningPointer<T>(new T(std::forward<Args>(args)...));
    }
}
