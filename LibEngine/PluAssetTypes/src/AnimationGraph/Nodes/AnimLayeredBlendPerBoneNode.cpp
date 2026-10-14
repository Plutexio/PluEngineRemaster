//
// Created by Plutex on 7/25/26.
//

#include "PluEngine/AssetTypes/AnimationGraph/Nodes/AnimLayeredBlendPerBoneNode.h"

namespace Plu
{
	String AnimLayeredBlendPerBoneNode::GetDisplayName()
	{
		return Bone.Name.IsEmpty() ? "Layered Blend per Bone" : "Layered Blend per Bone (" + Bone.Name + ")";
	}

	Pose AnimLayeredBlendPerBoneNode::Evaluate(AnimEvalContext& context)
	{
		PLU_PROFILE_SCOPE("AnimLayeredBlendPerBone");

		const float alpha = glm::clamp(ReadDataPin<float>(context, "Alpha", Alpha), 0.0f, 1.0f);

		// alpha <= 0, or no skeleton to resolve the bone against: "B" never needs evaluating — it can
		// be a whole animation sub-tree (same reasoning as AnimBlendByBoolNode's untaken branch).
		if (alpha <= 0.0f || !context.TargetSkeleton) {
			return EvaluateInputPose(context, "A");
		}

		const SkeletonPoseLayout& layout = context.TargetSkeleton->GetPoseLayout();
		const Int32 root = layout.FindIndex(Bone.Name);
		if (root < 0) {
			return EvaluateInputPose(context, "A");
		}

		const Pose a = EvaluateInputPose(context, "A");
		const Pose b = EvaluateInputPose(context, "B");

		DynamicArray<float> weights;
		layout.BuildSubtreeMask(root, alpha, 0.0f, weights);

		const int blendDepth = glm::max(ReadDataPin<int>(context, "BlendDepth", BlendDepth), 0);
		if (blendDepth > 0) {
			// Depth below Bone, one forward pass (ParentIndex[i] < i); -1 outside the subtree.
			DynamicArray<Int32> depth;
			depth.Resize(weights.Size());
			for (UInt64 i = 0; i < depth.Size(); ++i) depth[i] = -1;
			depth[static_cast<UInt64>(root)] = 0;
			for (UInt64 i = static_cast<UInt64>(root) + 1; i < depth.Size(); ++i) {
				const Int32 parent = layout.ParentIndex[i];
				if (parent >= 0 && depth[static_cast<UInt64>(parent)] >= 0) depth[i] = depth[static_cast<UInt64>(parent)] + 1;
			}
			for (UInt64 i = 0; i < depth.Size(); ++i) {
				if (depth[i] < 0) continue;
				weights[i] = alpha * glm::min(1.0f, static_cast<float>(depth[i] + 1) / static_cast<float>(blendDepth + 1));
			}
		}

		Pose result;
		BlendPosesMasked(a, b, weights, 0.0f, result);

		if (ReadDataPin<bool>(context, "MeshSpaceRotationBlend", MeshSpaceRotationBlend)) {
			// Re-solve each layered bone's local rotation so its component-space rotation is the blend
			// of A's and B's. Nodes are in DFS pre-order (ParentIndex[i] < i), so walking forward the
			// result's parent rotation is final by the time a child needs it.
			Pose globalsA, globalsB;
			layout.ComposeGlobals(a, globalsA);
			layout.ComposeGlobals(b, globalsB);

			const UInt64 count = glm::min(result.Size(), glm::min(globalsA.Size(), globalsB.Size()));
			DynamicArray<Quaternion> resultGlobalRotations;
			resultGlobalRotations.Resize(count);
			for (UInt64 i = 0; i < count; ++i) {
				const Int32 parent = layout.ParentIndex[i];
				const Quaternion parentRotation = (parent < 0)
					? Quaternion(1.0f, 0.0f, 0.0f, 0.0f) : resultGlobalRotations[static_cast<UInt64>(parent)];
				if (i < weights.Size() && weights[i] > 0.0f) {
					// Through BlendTransforms for its shortest-arc slerp; only the rotation is used.
					BoneTransform fromA, fromB;
					fromA.Rotation = globalsA[i].Rotation;
					fromB.Rotation = globalsB[i].Rotation;
					const Quaternion target = BlendTransforms(fromA, fromB, weights[i]).Rotation;
					result[i].Rotation = glm::normalize(glm::inverse(parentRotation) * target);
				}
				resultGlobalRotations[i] = parentRotation * result[i].Rotation;
			}
		}
		return result;
	}
}
