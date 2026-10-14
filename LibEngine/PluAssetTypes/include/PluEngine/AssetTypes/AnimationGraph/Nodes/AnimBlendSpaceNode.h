//
// Created by Plutex on 10/14/26.
//

#ifndef PLUENGINE_ANIMBLENDSPACENODE_H
#define PLUENGINE_ANIMBLENDSPACENODE_H

#include "PluEngine/AssetTypes/AnimationGraph/AnimGraphNode.h"
#include "PluEngine/AssetTypes/Animation/BlendSpace.h"
#include "AnimBlendSpaceNode.generated.h"

namespace Plu
{
	// Plays a BlendSpace asset: samples the clips around the input point and blends them by the
	// space's weights (BlendSpace::ComputeWeights).
	//
	// Clips are phase-synced: the node keeps one normalized phase per AnimGraphInstance, advanced at
	// the weight-averaged clip length, and every synced sample plays at that same phase — so a 1.0 s
	// walk and a 0.7 s run stay foot-aligned while blending. Samples flagged ExcludeFromSync (idle)
	// play on the graph clock instead. Without an instance (no per-user state) the phase falls back
	// to TimeSeconds / cycle length — correct for a constant input, drifts while it changes.
	//
	// Pins: "X" (and "Y" for a 2D space) float inputs, labelled with the asset's axis names; the pin
	// names stay "X"/"Y" so renaming an axis never breaks a link.
	PLU_STRUCT()
	struct PLUASSETTYPES_API AnimBlendSpaceNode : AnimGraphNode
	{
		REFLECTION_BODY_ANIMBLENDSPACENODE()

		PLU_PROPERTY()
		TUsePointer<BlendSpace> BlendSpaceAsset;

		// Fallback inputs while the X / Y pins are unconnected (axis units).
		PLU_PROPERTY()
		float X = 0.0f;
		PLU_PROPERTY()
		float Y = 0.0f;

		// Per-axis input smoothing (InterpToF speed, per instance). 0 = follow the input instantly.
		PLU_PROPERTY()
		float InterpSpeedX = 0.0f;
		PLU_PROPERTY()
		float InterpSpeedY = 0.0f;

		// Playback speed multiplier for every sample (synced phase and unsynced clocks alike).
		PLU_PROPERTY()
		float PlayRate = 1.0f;

		void BuildPins() override;
		[[nodiscard]] bool ArePinsStale() const override;
		String GetDisplayName() override;
		Pose Evaluate(AnimEvalContext& context) override;

	private:
		// What the current pins were built from — see ArePinsStale.
		const BlendSpace* mPinsBuiltFor = nullptr;
		UInt32 mPinsBuiltForRevision = 0;
	};
}

#endif //PLUENGINE_ANIMBLENDSPACENODE_H
