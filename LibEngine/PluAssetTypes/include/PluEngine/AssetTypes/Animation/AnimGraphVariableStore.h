//
// Created by Plutex on 7/24/26.
//

#ifndef PLUENGINE_ANIMGRAPHVARIABLESTORE_H
#define PLUENGINE_ANIMGRAPHVARIABLESTORE_H

#include "PluEngine/Core.h"
#include "PluEngine/AssetTypes/AnimationGraph/AnimationGraph.h"
#include "PluSTL/Array/Array.h"
#include "PluSTL/HashMap/HashMap.h"
#include "PluSTL/Pointers/TOwningPointer.h"
#include "PluSTL/Pointers/TUsePointer.h"
#include "PluSTL/String/String.h"

namespace Plu
{
	// Pure value store: name -> cloned variable. Knows nothing about nodes, graphs or assets — kept
	// out of AssetTypes/ on purpose so it can later back other node-graph domains, not just
	// AnimationGraph. Owned by AnimGraphInstance, one per (component, graph) pair.
	struct PLUASSETTYPES_API AnimGraphVariableStore
	{
		// Discards the current values and clones every default (asset load / first bind).
		void RebuildFrom(const DynamicArray<TOwningPointer<IAnimationGraphVariable>>& defaults);

		// Like RebuildFrom, but keeps the current value for any variable whose name AND TypeName
		// still match an existing entry — editing the variable list in PIE (add/rename/retype) must
		// not reset live state for the variables that didn't change. Bumps GetValueRevision() only
		// when something actually changed (added/removed/retyped), not on a no-op merge.
		void MergeFrom(const DynamicArray<TOwningPointer<IAnimationGraphVariable>>& defaults);

		[[nodiscard]] TUsePointer<IAnimationGraphVariable> Find(const String& name) const;

		// All live variables, in store order. Used by AnimGraphInstance::GetVariableNames (Python)
		// and by editor code that needs to list a live instance's variables.
		[[nodiscard]] const DynamicArray<TOwningPointer<IAnimationGraphVariable>>& GetAll() const { return mVariables; }

		// Bumped on RebuildFrom, a value-changing MergeFrom, and every successful Set<T> — the key
		// the pose cache (SkeletalMeshComponent::CachedPoseGraphValueRevision) compares against to
		// know a cached pose is stale even when nothing else about the source changed.
		[[nodiscard]] UInt32 GetValueRevision() const { return mValueRevision; }

		// Set<T> already bumps this on its own. This covers editor code that instead mutated a
		// Find()'d variable's value in place (e.g. ImGui editing a live PIE instance directly) — see
		// AnimationGraphDetailsPanel's live-instance mode.
		void MarkValueChanged() { ++mValueRevision; }

		// Type-safe via dynamic_cast<AnimationGraphVariable<T>*> — no type-id map, the concrete
		// template instantiation IS the type check. False when the name is unknown or T doesn't
		// match the variable's declared type.
		template <typename T>
		bool Set(const String& name, const T& value)
		{
			const UInt64* index = mNameToIndex.Find(name);
			if (!index) return false;
			IAnimationGraphVariable* variable = mVariables[*index].GetRaw();
			if (!variable || !dynamic_cast<AnimationGraphVariable<T>*>(variable)) return false;
			T* slot = static_cast<T*>(variable->GetData());
			// Only a real change bumps the revision. SkeletalMeshComponent::OnPreEvaluateAnimGraph
			// re-pushes every input it owns each frame, and most of those values are identical frame
			// to frame — an unconditional bump would invalidate the pose cache
			// (SkeletalMeshComponent::CachedPoseGraphValueRevision) on every single frame for every
			// component that has a hook, including ones standing perfectly still.
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
			IAnimationGraphVariable* variable = mVariables[*index].GetRaw();
			if (!variable || !dynamic_cast<AnimationGraphVariable<T>*>(variable)) return false;
			outValue = *static_cast<T*>(variable->GetData());
			return true;
		}

	private:
		DynamicArray<TOwningPointer<IAnimationGraphVariable>> mVariables;
		HashMap<String, UInt64> mNameToIndex;
		UInt32 mValueRevision = 0;
	};
}

#endif //PLUENGINE_ANIMGRAPHVARIABLESTORE_H
