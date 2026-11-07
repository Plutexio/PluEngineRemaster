//
// Created by Plutex on 7/21/26.
//

#ifndef PLUENGINE_ANIMGRAPHNODE_H
#define PLUENGINE_ANIMGRAPHNODE_H

#include "PluEngine/AssetTypes/NodeGraph/GraphNode.h"
#include "PluEngine/AssetTypes/NodeGraph/NodeGraph.h"
#include "PluEngine/AssetTypes/Animation/BoneTransform.h" // Pose
#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"
#include "AnimGraphNode.generated.h"

namespace Plu
{
	struct AnimGraphInstance;

	// Context threaded through a graph evaluation. Not reflected — rebuilt per evaluation call
	// by whoever drives the graph (e.g. SkeletalMeshComponent's tick), not persisted on the asset.
	// `Graph` and the data-cycle stack come from GraphEvalContext; a node that needs the animation
	// extras below reaches them by downcasting the base context (see AnimVariableNode).
	struct AnimEvalContext : GraphEvalContext
	{
		float TimeSeconds = 0.0f;
		bool Loop = true;

		// Graph-clock seconds since this instance's previous evaluation (0 when paused, on the
		// first evaluation, or when the clock jumped backwards). Filled by AnimationGraph::Evaluate
		// from AnimGraphInstance::LastEvalTimeSeconds; always 0 without an Instance. Nodes with
		// per-instance state (AnimBlendSpaceNode) advance it by this instead of by TimeSeconds.
		float DeltaSeconds = 0.0f;

		// Identifies one AnimationGraph::Evaluate call on one instance (0 without an Instance).
		// Lets stateful nodes advance only once even when evaluated several times — see
		// AnimNodeRuntimeState::LastEvaluationId.
		UInt64 EvaluationId = 0;

		// Skeleton this evaluation is driving. Needed to bind animation tracks by node index and
		// to fall back to the bind pose for unconnected inputs. Null is valid (e.g. previewing a
		// graph with no skeleton assigned yet) — nodes then evaluate to an empty pose.
		TUsePointer<Skeleton> TargetSkeleton;

		// Per-user variable values for this evaluation. Null => nodes fall back to the asset's default
		// values (e.g. editor preview of the graph with no bound SkeletalMeshComponent). Set by
		// whoever drives the graph (RenderSnapshotBuilder passes SkeletalMeshComponent's instance).
		AnimGraphInstance* Instance = nullptr;

		// World matrix of the component driving this evaluation. Only World-space nodes
		// (AnimTransformBoneNode) read it. Identity when unknown (e.g. editor graph preview with no
		// bound component) — World then degenerates to Component space.
		Matrix4 ComponentToWorld = Matrix4(1.0f);
	};

	// Category base for animation-graph nodes. Their flow pins carry poses (flow TypeId "Pose"),
	// and evaluation produces a Pose. Concrete nodes derive this. Abstract → not offered in palettes.
	PLU_STRUCT(Abstract)
	struct PLUASSETTYPES_API AnimGraphNode : GraphNode
	{
		REFLECTION_BODY_ANIMGRAPHNODE()

		// Flow kind for pose wires. A pose output only connects to a pose input (NodePin::CanConnect).
		static constexpr const char* PoseFlow = "Pose";

		// Evaluated by the graph runtime (traversal follows pose links to upstream nodes). Default:
		// empty pose — the fallback for nodes that don't produce one (e.g. a bare AnimGraphNode).
		virtual Pose Evaluate(AnimEvalContext& context) { return Pose(); }

	protected:
		void AddPoseInput(const String& name)  { AddPin(name, EPinDirection::Input,  EPinCategory::Flow, PoseFlow); }
		void AddPoseOutput(const String& name) { AddPin(name, EPinDirection::Output, EPinCategory::Flow, PoseFlow); }

		// Follows this node's `pinName` pose input across its link to the upstream node and
		// evaluates it. Unconnected input, or an upstream node that isn't an AnimGraphNode: falls
		// back to the target skeleton's bind pose (or an empty pose when no skeleton is set).
		[[nodiscard]] Pose EvaluateInputPose(AnimEvalContext& context, const String& pinName) const;

		// Data pins (parameters like AnimBlendNode::Alpha) are read through GraphNode::ReadDataPin —
		// the same helper every value node in every graph domain uses.
	};
}

#endif //PLUENGINE_ANIMGRAPHNODE_H
