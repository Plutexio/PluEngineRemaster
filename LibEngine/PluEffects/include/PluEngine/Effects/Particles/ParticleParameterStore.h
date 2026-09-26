//
// Created by Plutex on 9/27/26.
//

#ifndef PLUENGINE_PARTICLEPARAMETERSTORE_H
#define PLUENGINE_PARTICLEPARAMETERSTORE_H

#include "PluEngine/Core.h"
#include "PluEngine/Effects/Particles/ParticleParameter.h"
#include "Array/Array.h"
#include "HashMap/HashMap.h"

namespace Plu
{
	// One instance's live parameter values: a per-instance clone of the asset's parameter defaults.
	// Same shape as AnimGraphVariableStore. Order is the asset's parameter order, which is also the
	// order of CompiledParticleSystem::ParameterLayout, so WriteValueBlock needs no name lookups.
	struct PLUEFFECTS_API ParticleParameterStore
	{
		// Throws away every value and clones the defaults.
		void RebuildFrom(const DynamicArray<TOwningPointer<IParticleParameter>>& defaults);
		// The asset's parameter list changed: keeps live values of parameters that survived
		// (same name AND type), clones the rest.
		void MergeFrom(const DynamicArray<TOwningPointer<IParticleParameter>>& defaults);

		[[nodiscard]] TUsePointer<IParticleParameter> Find(const String& name) const;
		[[nodiscard]] const DynamicArray<TOwningPointer<IParticleParameter>>& GetAll() const { return mParameters; }

		// Bumped on every value change; consumers cache against it.
		[[nodiscard]] UInt32 GetValueRevision() const { return mValueRevision; }
		void MarkValueChanged() { ++mValueRevision; }

		// Writes every parameter into `dst` (floatCount floats) in layout order. Stops at floatCount, so a
		// block sized for an older layout is filled as far as it goes.
		void WriteValueBlock(float* dst, UInt32 floatCount) const;

		template <typename T>
		bool Set(const String& name, const T& value)
		{
			const UInt64* index = mNameToIndex.Find(name);
			if (!index) return false;
			IParticleParameter* parameter = mParameters[*index].GetRaw();
			if (!parameter || !dynamic_cast<ParticleParameter<T>*>(parameter)) return false;
			T* slot = static_cast<T*>(parameter->GetData());
			if (*slot == value) return true;
			*slot = value;
			++mValueRevision;
			return true;
		}

		template <typename T>
		bool TryGet(const String& name, T& outValue) const
		{
			const UInt64* index = mNameToIndex.Find(name);
			if (!index) return false;
			IParticleParameter* parameter = mParameters[*index].GetRaw();
			if (!parameter || !dynamic_cast<ParticleParameter<T>*>(parameter)) return false;
			outValue = *static_cast<T*>(parameter->GetData());
			return true;
		}

	private:
		DynamicArray<TOwningPointer<IParticleParameter>> mParameters;
		HashMap<String, UInt64> mNameToIndex;
		UInt32 mValueRevision = 0;
	};
}

#endif //PLUENGINE_PARTICLEPARAMETERSTORE_H
