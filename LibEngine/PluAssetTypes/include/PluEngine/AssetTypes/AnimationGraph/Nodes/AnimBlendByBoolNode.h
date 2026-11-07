//
// Created by Plutex on 7/25/26.
//

#ifndef PLUENGINE_ANIMBLENDBYBOOLNODE_H
#define PLUENGINE_ANIMBLENDBYBOOLNODE_H

#include "PluEngine/AssetTypes/AnimationGraph/AnimGraphNode.h"
#include "AnimBlendByBoolNode.generated.h"

namespace Plu
{
	// Picks one of two poses by a bool: true -> the "True" input, false -> the "False" input. Condition
	// is both an editable parameter (details panel) and, via BuildDataPinsFromReflection(), a bool data
	// input pin — so it is normally driven by a Boolean graph variable or a comparison node.
	//
	// The switch is instant. A timed cross-fade (UE's "Blend Poses by bool" blend time) would need
	// per-instance state (AnimGraphInstance::GetNodeState, see AnimBlendSpaceNode) — not implemented
	// yet. Blend an AnimBlendNode with a driven Alpha when a smooth transition is what you want.
	PLU_STRUCT()
	struct PLUASSETTYPES_API AnimBlendByBoolNode : AnimGraphNode
	{
		REFLECTION_BODY_ANIMBLENDBYBOOLNODE()

		// Fallback while the Condition pin is unconnected — same pattern as AnimBlendNode::Alpha.
		PLU_PROPERTY()
		bool Condition = false;

		void BuildPins() override
		{
			AddPoseInput("True");
			AddPoseInput("False");
			AddPoseOutput("Result");
			BuildDataPinsFromReflection(); // adds the "Condition" bool data input pin
		}

		String GetDisplayName() override { return "Blend Pose by Bool"; }

		Pose Evaluate(AnimEvalContext& context) override;
	};
}

#endif //PLUENGINE_ANIMBLENDBYBOOLNODE_H
