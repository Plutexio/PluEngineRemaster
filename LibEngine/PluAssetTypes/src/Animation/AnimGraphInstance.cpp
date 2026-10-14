//
// Created by Plutex on 7/24/26.
//

#include "PluEngine/AssetTypes/Animation/AnimGraphInstance.h"

namespace Plu
{
#ifdef PLU_ENGINE_EDITOR_BUILD
	namespace
	{
		void RemoveLiveInstance(UInt64 graphUuid, AnimGraphInstance* instance)
		{
			if (graphUuid == 0) return;
			DynamicArray<AnimGraphInstance*>& list = AnimGraphInstance::GetLiveInstances(graphUuid);
			for (UInt64 i = 0; i < list.Size(); ++i) {
				if (list[i] == instance) {
					list.RemoveAt(i);
					break;
				}
			}
		}
	}

	AnimGraphInstance::~AnimGraphInstance()
	{
		RemoveLiveInstance(mBoundGraphUuid, this);
	}

	DynamicArray<AnimGraphInstance*>& AnimGraphInstance::GetLiveInstances(UInt64 graphUuid)
	{
		static HashMap<UInt64, DynamicArray<AnimGraphInstance*>> sLiveInstances;
		return sLiveInstances[graphUuid];
	}
#endif

	void AnimGraphInstance::BindTo(const TUsePointer<AnimationGraph>& graph)
	{
		if (!graph) {
#ifdef PLU_ENGINE_EDITOR_BUILD
			RemoveLiveInstance(mBoundGraphUuid, this);
#endif
			mGraph = nullptr;
			mNodeStates.Clear();
			mBoundGraphUuid = 0;
			mBoundVariablesRevision = 0;
			return;
		}

		const UInt64 newUuid = graph->Uuid.getUUID();
		const bool sameAsset = mGraph && mBoundGraphUuid == newUuid;

		// Cheap no-op: still bound to the same asset at the same variable-list revision.
		if (sameAsset && mBoundVariablesRevision == graph->VariablesRevision) {
			mGraph = graph;
			return;
		}

		if (sameAsset) {
			// Same graph, variable list changed underneath us (edited in PIE): keep live values for
			// variables that survived, (re)clone the rest.
			mVariables.MergeFrom(graph->Variables);
		} else {
			// First bind, or the component was pointed at a different graph asset: no live state to
			// preserve.
#ifdef PLU_ENGINE_EDITOR_BUILD
			RemoveLiveInstance(mBoundGraphUuid, this);
#endif
			mVariables.RebuildFrom(graph->Variables);
			// Node states are keyed by the old graph's node uuids — meaningless for the new one.
			mNodeStates.Clear();
#ifdef PLU_ENGINE_EDITOR_BUILD
			GetLiveInstances(newUuid).PushBack(this);
#endif
		}

		mGraph = graph;
		mBoundGraphUuid = newUuid;
		mBoundVariablesRevision = graph->VariablesRevision;
	}

	bool AnimGraphInstance::SetFloat(const String& name, float value) { return Set<float>(name, value); }

	float AnimGraphInstance::GetFloat(const String& name, float fallback)
	{
		float value = fallback;
		TryGet<float>(name, value);
		return value;
	}

	bool AnimGraphInstance::SetInt(const String& name, int value) { return Set<int>(name, value); }

	int AnimGraphInstance::GetInt(const String& name, int fallback)
	{
		int value = fallback;
		TryGet<int>(name, value);
		return value;
	}

	bool AnimGraphInstance::SetBool(const String& name, bool value) { return Set<bool>(name, value); }

	bool AnimGraphInstance::GetBool(const String& name, bool fallback)
	{
		bool value = fallback;
		TryGet<bool>(name, value);
		return value;
	}

	bool AnimGraphInstance::SetString(const String& name, String value) { return Set<String>(name, value); }

	String AnimGraphInstance::GetString(const String& name, String fallback)
	{
		String value = fallback;
		TryGet<String>(name, value);
		return value;
	}

	bool AnimGraphInstance::SetVec3(const String& name, Vec3 value) { return Set<Vec3>(name, value); }

	Vec3 AnimGraphInstance::GetVec3(const String& name, Vec3 fallback)
	{
		Vec3 value = fallback;
		TryGet<Vec3>(name, value);
		return value;
	}

	bool AnimGraphInstance::HasVariable(const String& name) { return mVariables.Find(name).IsValid(); }

	DynamicArray<String> AnimGraphInstance::GetVariableNames()
	{
		DynamicArray<String> names;
		for (const TOwningPointer<IAnimationGraphVariable>& variable : mVariables.GetAll()) {
			if (variable) names.PushBack(variable->Name);
		}
		return names;
	}
}
