//
// Created by Plutex on 7/25/26.
//

#ifndef PLUENGINE_ANIMLAYEREDBLENDPERBONENODE_H
#define PLUENGINE_ANIMLAYEREDBLENDPERBONENODE_H

#include "PluEngine/AssetTypes/AnimationGraph/AnimGraphNode.h"
#include "PluEngine/AssetTypes/Animation/BoneRef.h"
#include "AnimLayeredBlendPerBoneNode.generated.h"

namespace Plu
{
	// Splits the skeleton in two at Bone: everything OUTSIDE its subtree comes from "A", Bone and its
	// whole subtree come from "B", weighted by Alpha. The classic "upper body from one animation, legs
	// from another" node. By default Alpha is a hard per-subtree weight — the transition at the chosen
	// joint is a sharp cut; BlendDepth ramps it in over the first few levels instead.
	PLU_STRUCT()
	struct PLUASSETTYPES_API AnimLayeredBlendPerBoneNode : AnimGraphNode
	{
		REFLECTION_BODY_ANIMLAYEREDBLENDPERBONENODE()

		// Root of the layered subtree. Not a pinnable type (see IsPinnableDataType) — details panel only.
		PLU_PROPERTY()
		BoneRef Bone;

		// Fallback while the Alpha pin is unconnected — same pattern as AnimBlendNode::Alpha.
		PLU_PROPERTY()
		float Alpha = 1.0f;

		// Blend the subtree's rotations in component space instead of parent space: the layered bones
		// keep B's orientation in the mesh whatever A's bones above them do. Without it, a layer
		// rooted at the spine still turns with A's hips — e.g. a strafe clip whose pelvis faces the
		// direction of travel twists an upper body taken from a forward walk along with it.
		// Locations and scales stay a parent-space blend either way.
		PLU_PROPERTY()
		bool MeshSpaceRotationBlend = false;

		// Spreads the cut over this many levels below Bone: the weight ramps 1/(N+1), 2/(N+1), ...
		// down the subtree and reaches Alpha N levels down. 0 = hard cut at Bone. With a mesh-space
		// layer at the spine this shares the turn between A's pelvis and B's chest over the
		// vertebrae, instead of folding all of it into one joint.
		PLU_PROPERTY()
		int BlendDepth = 0;

		void BuildPins() override
		{
			AddPoseInput("A");
			AddPoseInput("B");
			AddPoseOutput("Result");
			BuildDataPinsFromReflection(); // "Alpha", "MeshSpaceRotationBlend" and "BlendDepth" data input pins
		}

		String GetDisplayName() override;

		Pose Evaluate(AnimEvalContext& context) override;
	};
}

#endif //PLUENGINE_ANIMLAYEREDBLENDPERBONENODE_H
