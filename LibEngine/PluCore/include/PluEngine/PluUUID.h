//
// Created by Plutex on 1/3/26.
//

#ifndef PLUENGINE_PLUUUID_H
#define PLUENGINE_PLUUUID_H
#include "Core.h"
#include "String/String.h"

namespace Plu
{
	//0 is invalid UUID
	class PLUCORE_API PluUUID
	{
		UInt64 mUUID;
	public:
		PluUUID();
		PluUUID(UInt64 UUID);
		PluUUID(const PluUUID& other);
		~PluUUID() = default;

		bool operator ==(const PluUUID& other) const { return (mUUID == other.mUUID); }
		bool operator ==(const UInt64& other) const { return (mUUID == other); }
		bool operator !=(const UInt64& other) const { return (mUUID != other); }
		bool operator !=(const PluUUID& other) const { return (mUUID != other.mUUID); }
		PluUUID& operator =(const PluUUID& other);
		operator UInt64() const { return (mUUID); }

		[[nodiscard]] inline String toString() const
		{
			return String::FromInt(mUUID);
		}

		[[nodiscard]] inline UInt64 getUUID() const { return mUUID; }

		[[nodiscard]] inline bool isValid() const { return mUUID != 0; }

	};
}

#endif //PLUENGINE_PLUUUID_H