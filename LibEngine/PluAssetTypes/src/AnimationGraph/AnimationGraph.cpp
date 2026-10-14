//
// Created by Plutex on 7/21/26.
//

#include "PluEngine/AssetTypes/AnimationGraph/AnimationGraph.h"
#include "PluEngine/AssetTypes/Animation/AnimGraphInstance.h"
#include "PluEngine/AssetTypes/AnimationGraph/Nodes/AnimOutputPoseNode.h"
#include "PluEngine/AssetTypes/AnimationGraph/Nodes/AnimVariableNode.h"
// RegisterBuiltInTypes() instantiates AnimationGraphVariable<T>::DeSerialize/Serialize/
// DrawEditorControl (via RegisterType<T>) for T = int/float/bool/String/Vec3 — those bodies call
// TypeSerializer<T>, so the primitive specializations must be visible here, not just wherever the
// factory happens to be used from (this used to be Editor-only, where EditorApp.cpp already pulled
// this in; now Runtime needs it too, see Application::EngineInit).
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/Timer.h"

namespace Plu
{
	HashMap<String, AnimationGraphVariableFactory::VariableTypeInfo>& AnimationGraphVariableFactory::GetFactoryMap()
	{
		static HashMap<String, VariableTypeInfo> FactoryMap;
		return FactoryMap;
	}

	void AnimationGraphVariableFactory::RegisterBuiltInTypes()
	{
		// RegisterType<T>(display name, reflected pin type, colour). The pin type is the type spelling
		// nodes use in their PLU_PROPERTY declarations, so a variable node can wire into matching pins.
		RegisterType<int>("Integer", "int", Vec3(0, 255, 140));
		RegisterType<float>("Float", "float", Vec3(0, 255, 17));
		RegisterType<bool>("Boolean", "bool", Vec3(163, 3, 0));
		RegisterType<String>("String", "String", Vec3(176, 0, 172));
		RegisterType<Vec3>("Vec3", "Vec3", Vec3(255, 208, 0));
	}

	TUsePointer<IAnimationGraphVariable> AnimationGraph::FindVariable(const String& name)
	{
		for (TOwningPointer<IAnimationGraphVariable>& variable : Variables) {
			if (variable && variable->Name == name) return variable;
		}
		return nullptr;
	}

	AnimVariableNode* AnimationGraph::AddVariableNode(const TUsePointer<IAnimationGraphVariable>& variable)
	{
		if (!variable) return nullptr;
		auto* node = static_cast<AnimVariableNode*>(AnimVariableNode::GetStaticClass()->Construct());
		if (!node) return nullptr;
		node->Variable     = variable;
		node->VariableName = variable->Name;
		node->BuildPins();
		Nodes.PushBack(TOwningPointer<GraphNode>(node));
		return node;
	}

	void AnimationGraph::ResolveVariableReferences()
	{
		for (TOwningPointer<GraphNode>& node : Nodes) {
			auto* variableNode = dynamic_cast<AnimVariableNode*>(node.GetRaw());
			if (!variableNode) continue;
			variableNode->Variable = FindVariable(variableNode->VariableName);
			// Pins were already built once during load (with no bound variable, so an untyped pin);
			// clear before rebuilding or the pin would be duplicated.
			variableNode->InputPins.Clear();
			variableNode->OutputPins.Clear();
			variableNode->BuildPins();
		}
	}

	void AnimationGraph::SyncVariableNodeNames()
	{
		for (TOwningPointer<GraphNode>& node : Nodes) {
			auto* variableNode = dynamic_cast<AnimVariableNode*>(node.GetRaw());
			if (variableNode && variableNode->Variable) {
				variableNode->VariableName = variableNode->Variable->Name;
			}
		}
	}

	bool AnimationGraph::IsCompatibleWith(const TUsePointer<Skeleton>& skeleton) const
	{
		if (!TargetSkeleton) return true;
		if (!skeleton) return false;
		return TargetSkeleton->Uuid.getUUID() == skeleton->Uuid.getUUID();
	}

	Pose AnimationGraph::Evaluate(AnimEvalContext& context)
	{
		PLU_PROFILE_SCOPE("AnimationGraph::Evaluate");
		context.Graph = this;
		// Callers that know the concrete mesh (RenderSnapshotBuilder) pass its skeleton; anyone
		// evaluating the asset on its own (editor preview) gets the graph's authoring skeleton.
		if (!context.TargetSkeleton)
			context.TargetSkeleton = TargetSkeleton;
		if (context.Instance)
		{
			// Delta from the graph clock rather than the frame's dt: a paused component (clock
			// frozen) gets 0, so stateful nodes hold still exactly like the stateless ones do.
			AnimGraphInstance& instance = *context.Instance;
			const float delta = context.TimeSeconds - instance.LastEvalTimeSeconds;
			context.DeltaSeconds = delta > 0.0f ? delta : 0.0f;
			instance.LastEvalTimeSeconds = context.TimeSeconds;
			context.EvaluationId = ++instance.EvaluationId;
		}
		for (TOwningPointer<GraphNode>& node : Nodes)
		{
			if (auto* output = dynamic_cast<AnimOutputPoseNode*>(node.GetRaw()))
				return output->Evaluate(context);
		}
		return Pose();
	}
}
