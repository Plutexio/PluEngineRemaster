//
// Created by Plutex on 7/24/26.
//

#include "PluEngine/AssetTypes/Animation/AnimGraphVariableStore.h"

namespace Plu
{
	void AnimGraphVariableStore::RebuildFrom(const DynamicArray<TOwningPointer<IAnimationGraphVariable>>& defaults)
	{
		mVariables.Clear();
		mNameToIndex.Clear();
		for (const TOwningPointer<IAnimationGraphVariable>& def : defaults) {
			if (!def) continue;
			mNameToIndex.Insert(def->Name, mVariables.Size());
			mVariables.PushBack(def->Clone());
		}
		++mValueRevision;
	}

	void AnimGraphVariableStore::MergeFrom(const DynamicArray<TOwningPointer<IAnimationGraphVariable>>& defaults)
	{
		DynamicArray<TOwningPointer<IAnimationGraphVariable>> merged;
		HashMap<String, UInt64> mergedIndex;
		bool changed = defaults.Size() != mVariables.Size();

		for (const TOwningPointer<IAnimationGraphVariable>& def : defaults) {
			if (!def) continue;

			TOwningPointer<IAnimationGraphVariable> next;
			if (const UInt64* existingIndex = mNameToIndex.Find(def->Name)) {
				IAnimationGraphVariable* existing = mVariables[*existingIndex].GetRaw();
				if (existing && existing->TypeName == def->TypeName) {
					next = mVariables[*existingIndex]; // same name+type: keep the live value
				}
			}
			if (!next) {
				next = def->Clone();
				changed = true;
			}

			mergedIndex.Insert(def->Name, merged.Size());
			merged.PushBack(next);
		}

		mVariables = std::move(merged);
		mNameToIndex = std::move(mergedIndex);
		if (changed) ++mValueRevision;
	}

	TUsePointer<IAnimationGraphVariable> AnimGraphVariableStore::Find(const String& name) const
	{
		if (const UInt64* index = mNameToIndex.Find(name)) {
			return mVariables[*index];
		}
		return nullptr;
	}
}
