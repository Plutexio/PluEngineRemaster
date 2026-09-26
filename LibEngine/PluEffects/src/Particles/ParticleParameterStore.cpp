//
// Created by Plutex on 9/27/26.
//

#include "PluEngine/Effects/Particles/ParticleParameterStore.h"

namespace Plu
{
	void ParticleParameterStore::RebuildFrom(const DynamicArray<TOwningPointer<IParticleParameter>>& defaults)
	{
		mParameters.Clear();
		mNameToIndex.Clear();
		for (const TOwningPointer<IParticleParameter>& def : defaults) {
			if (!def) continue;
			mNameToIndex.Insert(def->Name, mParameters.Size());
			mParameters.PushBack(def->Clone());
		}
		++mValueRevision;
	}

	void ParticleParameterStore::MergeFrom(const DynamicArray<TOwningPointer<IParticleParameter>>& defaults)
	{
		DynamicArray<TOwningPointer<IParticleParameter>> merged;
		HashMap<String, UInt64> mergedIndex;
		bool changed = defaults.Size() != mParameters.Size();

		for (const TOwningPointer<IParticleParameter>& def : defaults) {
			if (!def) continue;

			TOwningPointer<IParticleParameter> next;
			if (const UInt64* existingIndex = mNameToIndex.Find(def->Name)) {
				IParticleParameter* existing = mParameters[*existingIndex].GetRaw();
				if (existing && existing->TypeName == def->TypeName) next = mParameters[*existingIndex];
			}
			if (!next) {
				next = def->Clone();
				changed = true;
			}
			mergedIndex.Insert(def->Name, merged.Size());
			merged.PushBack(next);
		}

		mParameters = std::move(merged);
		mNameToIndex = std::move(mergedIndex);
		if (changed) ++mValueRevision;
	}

	TUsePointer<IParticleParameter> ParticleParameterStore::Find(const String& name) const
	{
		if (const UInt64* index = mNameToIndex.Find(name)) return mParameters[*index];
		return nullptr;
	}

	void ParticleParameterStore::WriteValueBlock(float* dst, UInt32 floatCount) const
	{
		UInt32 offset = 0;
		for (const TOwningPointer<IParticleParameter>& parameter : mParameters) {
			if (!parameter) continue;
			const UInt32 count = parameter->GetFloatCount();
			if (offset + count > floatCount) return;
			parameter->WriteFloats(dst + offset);
			offset += count;
		}
	}
}
