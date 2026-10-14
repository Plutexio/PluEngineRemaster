//
// Created by Plutex on 7/21/26.
//

#include "PluEngine/AssetTypes/AnimationGraph/Nodes/AnimSampleNode.h"

#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

namespace Plu
{
	String AnimSampleNode::GetDisplayName()
	{
#ifdef PLU_ENGINE_EDITOR_BUILD
		if (AnimationAsset) {
			return TypeRegistry::GetInstance()->GetAssetManager()->GetAssetDescriptor(AnimationAsset->Uuid)->AssetName;
		} else {
			return "Sample Animation";
		}
#else
		return "Sample Animation";
#endif
	}

	Pose AnimSampleNode::Evaluate(AnimEvalContext& context)
	{
		if (!context.TargetSkeleton) return Pose();

		Pose pose;
		if (!AnimationAsset)
		{
			context.TargetSkeleton->GetPoseLayout().MakeBindPose(pose);
			return pose;
		}
		SampleAnimationPose(*AnimationAsset, *context.TargetSkeleton, context.TimeSeconds, context.Loop, pose);
		return pose;
	}
}
