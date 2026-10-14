//
// Created by Plutex on 10/14/26.
//

#include "PluEngine/AssetTypes/AnimationGraph/Nodes/AnimBlendSpaceNode.h"

#include <cmath>

#include "PluEngine/AssetTypes/Animation/AnimGraphInstance.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Timer.h"

namespace Plu
{
	namespace
	{
		// Per-instance state of one AnimBlendSpaceNode (AnimGraphInstance::GetNodeState).
		struct BlendSpaceNodeState : AnimNodeRuntimeState
		{
			Vec2 SmoothedInput = Vec2(0.0f);
			double Phase = 0.0; // normalized [0, 1) position in the synced cycle
			bool Initialized = false;
		};

		double WrapOrClampPhase(double phase, bool loop)
		{
			if (loop) return phase - std::floor(phase);
			return std::clamp(phase, 0.0, 1.0);
		}
	}

	void AnimBlendSpaceNode::BuildPins()
	{
		AddPoseOutput("Pose");

		const BlendSpace* space = BlendSpaceAsset.GetRaw();
		AddPin("X", EPinDirection::Input, EPinCategory::Data, "float");
		if (space) FindPin("X", EPinDirection::Input)->Label = space->AxisX.Name;
		if (space && space->IsTwoD()) {
			AddPin("Y", EPinDirection::Input, EPinCategory::Data, "float");
			FindPin("Y", EPinDirection::Input)->Label = space->AxisY.Name;
		}

		mPinsBuiltFor = space;
		mPinsBuiltForRevision = space ? space->GetRevision() : 0;
	}

	bool AnimBlendSpaceNode::ArePinsStale() const
	{
		const BlendSpace* space = BlendSpaceAsset.GetRaw();
		if (space != mPinsBuiltFor) return true;
		return space && space->GetRevision() != mPinsBuiltForRevision;
	}

	String AnimBlendSpaceNode::GetDisplayName()
	{
#ifdef PLU_ENGINE_EDITOR_BUILD
		if (BlendSpaceAsset) {
			if (auto descriptor = TypeRegistry::GetInstance()->GetAssetManager()->GetAssetDescriptor(BlendSpaceAsset->Uuid))
				return descriptor->AssetName;
		}
#endif
		return "Blend Space";
	}

	Pose AnimBlendSpaceNode::Evaluate(AnimEvalContext& context)
	{
		PLU_PROFILE_SCOPE("AnimBlendSpaceNode::Evaluate");
		if (!context.TargetSkeleton) return Pose();

		const Skeleton& skeleton = *context.TargetSkeleton;
		const SkeletonPoseLayout& layout = skeleton.GetPoseLayout();
		if (!layout.IsValid()) return Pose();

		Pose result;
		if (!BlendSpaceAsset || BlendSpaceAsset->Samples.IsEmpty()) {
			layout.MakeBindPose(result);
			return result;
		}
		const BlendSpace& space = *BlendSpaceAsset;

		const Vec2 target = space.ClampInput(Vec2(
			ReadDataPin<float>(context, "X", X),
			space.IsTwoD() ? ReadDataPin<float>(context, "Y", Y) : 0.0f));

		// Per-instance state: smoothed input + sync phase. Advanced once per graph evaluation even
		// when this node's output feeds several consumers.
		BlendSpaceNodeState* state = context.Instance
			? &context.Instance->GetNodeState<BlendSpaceNodeState>(Uuid.getUUID())
			: nullptr;
		const bool advance = state && state->LastEvaluationId != context.EvaluationId;
		Vec2 input = target;
		if (state) {
			if (advance) {
				state->LastEvaluationId = context.EvaluationId;
				if (!state->Initialized) {
					// First evaluation snaps to the input instead of easing in from the origin.
					state->SmoothedInput = target;
					state->Initialized = true;
				} else {
					const float dt = context.DeltaSeconds;
					state->SmoothedInput.x = InterpSpeedX > 0.0f ? InterpToF(state->SmoothedInput.x, target.x, dt, InterpSpeedX) : target.x;
					state->SmoothedInput.y = InterpSpeedY > 0.0f ? InterpToF(state->SmoothedInput.y, target.y, dt, InterpSpeedY) : target.y;
				}
			}
			input = state->SmoothedInput;
		}

		DynamicArray<BlendSampleWeight> weights;
		space.ComputeWeights(input, weights);

		// Synced cycle length = weight-averaged length of the synced samples in play.
		double syncedWeight = 0.0;
		double syncedDuration = 0.0;
		for (const BlendSampleWeight& weight : weights) {
			const BlendSample& sample = space.Samples[weight.SampleIndex];
			if (sample.ExcludeFromSync || !sample.AnimationAsset) continue;
			const double duration = sample.AnimationAsset->GetDurationSeconds();
			if (duration <= 0.0) continue;
			syncedDuration += weight.Weight * duration;
			syncedWeight += weight.Weight;
		}
		const double cycleSeconds = syncedWeight > 0.0 ? syncedDuration / syncedWeight : 0.0;

		double phase = 0.0;
		if (state) {
			if (advance && cycleSeconds > 0.0)
				state->Phase = WrapOrClampPhase(state->Phase + context.DeltaSeconds * PlayRate / cycleSeconds, context.Loop);
			phase = state->Phase;
		} else if (cycleSeconds > 0.0) {
			phase = WrapOrClampPhase(context.TimeSeconds * PlayRate / cycleSeconds, context.Loop);
		}

		// N-way blend, folded pairwise: after each step `result` is the normalized blend of every
		// sample so far, so sample i enters at weight_i / (sum of weights up to i).
		Pose samplePose;
		float accumulated = 0.0f;
		for (const BlendSampleWeight& weight : weights) {
			const BlendSample& sample = space.Samples[weight.SampleIndex];
			Pose& destination = accumulated > 0.0f ? samplePose : result;
			if (sample.AnimationAsset) {
				const double time = sample.ExcludeFromSync
					? static_cast<double>(context.TimeSeconds) * PlayRate
					: phase * sample.AnimationAsset->GetDurationSeconds();
				SampleAnimationPose(*sample.AnimationAsset, skeleton, time, context.Loop, destination);
			} else {
				layout.MakeBindPose(destination);
			}

			accumulated += weight.Weight;
			if (&destination != &result)
				BlendPoses(result, samplePose, weight.Weight / accumulated, result);
		}
		return result;
	}
}
