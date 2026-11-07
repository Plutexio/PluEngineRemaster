//
// Created by Plutex on 7/7/26.
//

#ifndef PLUENGINE_SKELETALANIMATION_H
#define PLUENGINE_SKELETALANIMATION_H

#include "PluEngine/Core.h"
#include "PluEngine/Core/IAssetData.h"
#include "SkeletalAnimation.generated.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/AssetTypes/Animation/BoneTransform.h" // Pose

namespace Plu
{
    struct Skeleton;
    struct SkeletonNode;

    PLU_STRUCT()
    struct PLUASSETTYPES_API AnimationVectorKey
    {
        REFLECTION_BODY_ANIMATIONVECTORKEY()

        double Timestamp; // assimp ticks

        Vec3 Value;
    };

    PLU_STRUCT()
    struct PLUASSETTYPES_API AnimationQuatKey
    {
        REFLECTION_BODY_ANIMATIONQUATKEY()

        double Timestamp; // assimp ticks

        Quaternion Value;
    };

    PLU_STRUCT()
    struct PLUASSETTYPES_API AnimationTrack
    {
        REFLECTION_BODY_ANIMATIONTRACK()

        TUsePointer<SkeletonNode> Node;

        // Per-channel keys, sorted by Timestamp ascending (ticks). A channel may be empty
        // (FBX pivot-split nodes often animate a single component) — sampling then returns
        // the fallback, whose defaults mirror the identity components the importer assumes.
        DynamicArray<AnimationVectorKey> LocationKeys;
        DynamicArray<AnimationQuatKey> RotationKeys;
        DynamicArray<AnimationVectorKey> ScaleKeys;

        // Sample at a time in ticks: interpolates between the surrounding keys
        // (lerp, slerp for rotation) and clamps outside the key range.
        Vec3 GetLocationAtTime(double timeTicks, const Vec3& fallback = Vec3(0.0f)) const;
        Quaternion GetRotationAtTime(double timeTicks, const Quaternion& fallback = Quaternion(1.0f, 0.0f, 0.0f, 0.0f)) const;
        Vec3 GetScaleAtTime(double timeTicks, const Vec3& fallback = Vec3(1.0f)) const;

        void SortKeys();
    };

    PLU_STRUCT()
    struct PLUASSETTYPES_API Animation : IAssetData
    {
        REFLECTION_BODY_ANIMATION()

        TUsePointer<Skeleton> AnimationSkeleton;

        int FramesAmount;      // duration in ticks; valid sample times are [0, FramesAmount]
        float FramesPerSecond; // ticks per second

        HashMap<String, AnimationTrack> Tracks;

        // Tracks resolved against a skeleton's SkeletonPoseLayout: entry [i] is the track driving
        // node i, or nullptr when this animation does not animate that node. Lets pose evaluation
        // index straight into the array instead of hashing NodeName per node per frame.
        //
        // Built on first use and cached for one skeleton at a time (an animation is authored
        // against a single skeleton, so the slot effectively never thrashes); shared by every
        // component playing this animation. Main thread only — the result points into Tracks, so
        // code that mutates Tracks in place must call InvalidateTrackBinding (adding or removing
        // tracks is detected automatically, editing an existing one in place is not).
        const DynamicArray<const AnimationTrack*>& GetTrackBinding(const Skeleton& skeleton) const;

        void InvalidateTrackBinding() const;

        // Length in seconds (FramesAmount / FramesPerSecond); 0 for an empty or FPS-less clip.
        [[nodiscard]] double GetDurationSeconds() const;

    private:
        mutable DynamicArray<const AnimationTrack*> mTrackBinding;
        mutable UInt64 mTrackBindingSkeletonUuid = 0;
        mutable UInt64 mTrackBindingTrackCount = 0;
        mutable bool mTrackBindingBuilt = false;
    };

    // Samples `animation` at `timeSeconds` into outPose (local space, indexed by skeleton's
    // SkeletonPoseLayout). Loop wraps the time into [0, duration); otherwise it clamps, holding the
    // first/last frame. Nodes the clip doesn't animate keep their bind transform. Returns false and
    // leaves outPose empty when the skeleton has no valid pose layout. Main thread only (the track
    // binding cache — see Animation::GetTrackBinding).
    PLUASSETTYPES_API bool SampleAnimationPose(const Animation& animation, const Skeleton& skeleton,
                                               double timeSeconds, bool loop, Pose& outPose);
}

#endif //PLUENGINE_SKELETALANIMATION_H
