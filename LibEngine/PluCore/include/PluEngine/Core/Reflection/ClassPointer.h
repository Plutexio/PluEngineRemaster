//
// Created by Plutex on 2026-02-14.
//

#ifndef PLUENGINE_CLASSPOINTER_H
#define PLUENGINE_CLASSPOINTER_H
#include "ReflectionBase.h"
#include "PluEngine/Core/Objects/EngineObject.h"

namespace Plu
{
	class EngineObject;

	template<typename T>
	class TClassPointer
	{
		static_assert(EngineObjectConc<T>, "TClassPointer, T must derive from EngineObject or be it");

		TypeInfo* type;

		template<typename U>
		friend class TClassPointer;
	public:
		TClassPointer()
		{
			type = nullptr;
		}

		TClassPointer(TypeInfo* typeInfo)
		{
			type = typeInfo;
		}

		TClassPointer(const TClassPointer& other)
		{
			type = other.type;
		}

		TClassPointer(TClassPointer&& other) noexcept
		{
			type = other.type;
			other.type = nullptr;
		}

		template<typename U>
		TClassPointer(const TClassPointer<U>& other)
		{
			static_assert(std::is_base_of_v<T, U> || std::is_same_v<T,U>, "U must be same or derive from T");
			type = other.type;
		}

		TClassPointer& operator=(TypeInfo* typeInfo)
		{
			if (!typeInfo) return *this;
			assert(typeInfo->IsDerivedOfOrSame(T::GetStaticClass()) && "Type is not derived from T");
			type = typeInfo;
			return *this;
		}

		TClassPointer& operator=(const TClassPointer& other)
		{
			assert((!other.type || other.type->IsDerivedOfOrSame(T::GetStaticClass())) && "Type is not derived from T");
			if (this != &other) {
				type = other.type;
			}
			return *this;
		}

		TClassPointer& operator=(TClassPointer&& other) noexcept
		{
			assert((!other.type || other.type->IsDerivedOfOrSame(T::GetStaticClass())) && "Type is not derived from T");
			if (this != &other) {
				type = other.type;
				other.type = nullptr;
			}
			return *this;
		}

		[[nodiscard]] TypeInfo* GetRawType() const
		{
			return type;
		}

		[[nodiscard]] TypeInfo* GetTType() const
		{
			return T::GetStaticClass();
		}

		operator TypeInfo*() const
		{
			return GetRawType();
		}
	};
}

#endif //PLUENGINE_CLASSPOINTER_H