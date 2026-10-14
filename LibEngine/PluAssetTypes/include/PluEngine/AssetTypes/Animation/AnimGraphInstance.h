//
// Created by Plutex on 7/24/26.
//

#ifndef PLUENGINE_ANIMGRAPHINSTANCE_H
#define PLUENGINE_ANIMGRAPHINSTANCE_H

#include "PluEngine/Core.h"
#include "PluEngine/AssetTypes/Animation/AnimGraphVariableStore.h"
#include "Pointers/TUsePointer.h"
#include "Pointers/Casts.h"
#include "HashMap/HashMap.h"
#include "AnimGraphInstance.generated.h"

namespace Plu
{
	struct AnimationGraph;

	// Base for a graph node's per-instance runtime state (e.g. AnimBlendSpaceNode's sync phase and
	// smoothed input). Graph nodes live on the shared asset, so anything that must persist between
	// evaluations of ONE user goes here instead, keyed by the node's uuid
	// (AnimGraphInstance::GetNodeState). Not reflected — pure runtime, never serialized.
	struct PLUASSETTYPES_API AnimNodeRuntimeState
	{
		virtual ~AnimNodeRuntimeState() = default;

		// AnimEvalContext::EvaluationId of the last evaluation that advanced this state. A node
		// whose output feeds several consumers is evaluated more than once per graph evaluation;
		// comparing against this keeps time-based state (phase, smoothing) advancing only once.
		UInt64 LastEvaluationId = 0;
	};

	// Per-user live values for one AnimationGraph. Owned by SkeletalMeshComponent
	// (SkeletalMeshComponent::EnsureAnimGraphInstance), never the asset — two components driving the
	// same graph asset each get their own instance, so SetFloat("Speed", ...) on one never touches
	// the other. See AnimGraphVariableStore for the value storage itself.
	PLU_STRUCT(PyExport)
	struct PLUASSETTYPES_API AnimGraphInstance
	{
		REFLECTION_BODY_ANIMGRAPHINSTANCE()

#ifdef PLU_ENGINE_EDITOR_BUILD
		~AnimGraphInstance();
#endif

		// (Re)binds to `graph`: clones its default variables into this instance's store. Cheap no-op
		// when already bound to the same asset uuid at the same VariablesRevision — a change in
		// either (new graph, or the same graph's variable list edited in PIE) triggers a
		// RebuildFrom/MergeFrom on the underlying store.
		void BindTo(const TUsePointer<AnimationGraph>& graph);
		[[nodiscard]] TUsePointer<AnimationGraph> GetGraph() const { return mGraph; }
		AnimGraphVariableStore& GetVariables() { return mVariables; }

		// --- C++ ---
		template <typename T>
		bool Set(const String& name, const T& value) { return mVariables.Set<T>(name, value); }

		template <typename T>
		bool TryGet(const String& name, T& outValue) const { return mVariables.TryGet<T>(name, outValue); }

		// --- Python (typed: pybind11 can't bind the Set<T>/TryGet<T> templates above) ---
		PLU_FUNCTION(PyExport)
		bool SetFloat(const String& name, float value);
		PLU_FUNCTION(PyExport)
		float GetFloat(const String& name, float fallback = 0.0f);
		PLU_FUNCTION(PyExport)
		bool SetInt(const String& name, int value);
		PLU_FUNCTION(PyExport)
		int GetInt(const String& name, int fallback = 0);
		PLU_FUNCTION(PyExport)
		bool SetBool(const String& name, bool value);
		PLU_FUNCTION(PyExport)
		bool GetBool(const String& name, bool fallback = false);
		PLU_FUNCTION(PyExport)
		bool SetString(const String& name, String value);
		PLU_FUNCTION(PyExport)
		String GetString(const String& name, String fallback = String());
		PLU_FUNCTION(PyExport)
		bool SetVec3(const String& name, Vec3 value);
		PLU_FUNCTION(PyExport)
		Vec3 GetVec3(const String& name, Vec3 fallback = Vec3(0.0f));
		PLU_FUNCTION(PyExport)
		bool HasVariable(const String& name);
		PLU_FUNCTION(PyExport)
		DynamicArray<String> GetVariableNames();

		// Runtime state of node `nodeUuid` (GraphNode::Uuid), created on first use. T must derive
		// AnimNodeRuntimeState and be default-constructible; one node always asks for the same T —
		// a mismatch (node uuid reused by a different node type after an edit) resets the state.
		template <typename T>
		T& GetNodeState(UInt64 nodeUuid)
		{
			if (TOwningPointer<AnimNodeRuntimeState>* existing = mNodeStates.Find(nodeUuid)) {
				if (T* typed = dynamic_cast<T*>(existing->GetRaw())) return *typed;
			}
			TOwningPointer<T> created = CreateOwning<T>();
			T* raw = created.GetRaw();
			mNodeStates.InsertOrAssign(nodeUuid, TOwningPointer<AnimNodeRuntimeState>(std::move(created)));
			return *raw;
		}

		// Drops every node's runtime state (phases restart, smoothed inputs snap to their targets).
		void ResetNodeStates() { mNodeStates.Clear(); }

		// Graph clock (AnimEvalContext::TimeSeconds) at the previous evaluation — the next one
		// derives AnimEvalContext::DeltaSeconds from it. Maintained by AnimationGraph::Evaluate.
		float LastEvalTimeSeconds = 0.0f;

		// Bumped once per AnimationGraph::Evaluate; copied into AnimEvalContext::EvaluationId.
		UInt64 EvaluationId = 0;

#ifdef PLU_ENGINE_EDITOR_BUILD
		// Debug label set by SkeletalMeshComponent ("<GameObject> / <Component>") so the Variables
		// panel's PIE instance picker has something readable to show.
		String DebugName;

		// Live instances for one graph asset, keyed by its uuid — registered in BindTo, dropped in
		// the destructor. Raw pointers: the owning SkeletalMeshComponent (via
		// TOwningPointer<AnimGraphInstance>) is always the real owner.
		static DynamicArray<AnimGraphInstance*>& GetLiveInstances(UInt64 graphUuid);
#endif

	private:
		TUsePointer<AnimationGraph> mGraph;
		AnimGraphVariableStore mVariables;
		HashMap<UInt64, TOwningPointer<AnimNodeRuntimeState>> mNodeStates;
		UInt64 mBoundGraphUuid = 0;
		UInt32 mBoundVariablesRevision = 0;
	};
}

#endif //PLUENGINE_ANIMGRAPHINSTANCE_H
