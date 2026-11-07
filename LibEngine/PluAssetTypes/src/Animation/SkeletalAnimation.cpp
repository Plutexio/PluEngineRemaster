//
// Created by Plutex on 7/7/26.
//

#include "PluEngine/AssetTypes/Animation/SkeletalAnimation.h"

#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"
#include "PluEngine/Timer.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    // Index of the last key with Timestamp <= time, or -1 when time precedes all keys.
    template <typename KeyT>
    Int64 LastKeyAtOrBefore(const DynamicArray<KeyT>& keys, double time)
    {
        Int64 lo = 0;
        Int64 hi = static_cast<Int64>(keys.Size()) - 1;
        Int64 result = -1;
        while (lo <= hi)
        {
            const Int64 mid = lo + (hi - lo) / 2;
            if (keys[static_cast<UInt64>(mid)].Timestamp <= time)
            {
                result = mid;
                lo = mid + 1;
            }
            else
            {
                hi = mid - 1;
            }
        }
        return result;
    }

    // 0..1 blend factor between two neighbouring keys (0 when they share a timestamp).
    template <typename KeyT>
    float BlendFactor(const KeyT& a, const KeyT& b, double time)
    {
        const double span = b.Timestamp - a.Timestamp;
        return span > 0.0 ? static_cast<float>((time - a.Timestamp) / span) : 0.0f;
    }
}

Vec3 Plu::AnimationTrack::GetLocationAtTime(double timeTicks, const Vec3& fallback) const
{
    if (LocationKeys.IsEmpty()) return fallback;

    const Int64 i = LastKeyAtOrBefore(LocationKeys, timeTicks);
    if (i < 0) return LocationKeys.Front().Value;
    if (i >= static_cast<Int64>(LocationKeys.Size()) - 1) return LocationKeys.Back().Value;

    const AnimationVectorKey& a = LocationKeys[static_cast<UInt64>(i)];
    const AnimationVectorKey& b = LocationKeys[static_cast<UInt64>(i) + 1];
    return glm::mix(a.Value, b.Value, BlendFactor(a, b, timeTicks));
}

Quaternion Plu::AnimationTrack::GetRotationAtTime(double timeTicks, const Quaternion& fallback) const
{
    if (RotationKeys.IsEmpty()) return fallback;

    const Int64 i = LastKeyAtOrBefore(RotationKeys, timeTicks);
    if (i < 0) return RotationKeys.Front().Value;
    if (i >= static_cast<Int64>(RotationKeys.Size()) - 1) return RotationKeys.Back().Value;

    const AnimationQuatKey& a = RotationKeys[static_cast<UInt64>(i)];
    const AnimationQuatKey& b = RotationKeys[static_cast<UInt64>(i) + 1];
    return glm::normalize(glm::slerp(a.Value, b.Value, BlendFactor(a, b, timeTicks)));
}

Vec3 Plu::AnimationTrack::GetScaleAtTime(double timeTicks, const Vec3& fallback) const
{
    if (ScaleKeys.IsEmpty()) return fallback;

    const Int64 i = LastKeyAtOrBefore(ScaleKeys, timeTicks);
    if (i < 0) return ScaleKeys.Front().Value;
    if (i >= static_cast<Int64>(ScaleKeys.Size()) - 1) return ScaleKeys.Back().Value;

    const AnimationVectorKey& a = ScaleKeys[static_cast<UInt64>(i)];
    const AnimationVectorKey& b = ScaleKeys[static_cast<UInt64>(i) + 1];
    return glm::mix(a.Value, b.Value, BlendFactor(a, b, timeTicks));
}

void Plu::AnimationTrack::SortKeys()
{
    const auto byTime = [](const auto& a, const auto& b) { return a.Timestamp < b.Timestamp; };
    LocationKeys.Sort(byTime);
    RotationKeys.Sort(byTime);
    ScaleKeys.Sort(byTime);
}

const DynamicArray<const Plu::AnimationTrack*>& Plu::Animation::GetTrackBinding(const Skeleton& skeleton) const
{
    const SkeletonPoseLayout& layout = skeleton.GetPoseLayout();
    const UInt64 skeletonUuid = skeleton.Uuid.getUUID();

    // Rebuild when bound to a different skeleton, or when tracks were added/removed since
    // (the stored pointers would then reference a rehashed map).
    const bool stale = !mTrackBindingBuilt
        || mTrackBindingSkeletonUuid != skeletonUuid
        || mTrackBindingTrackCount != Tracks.Size()
        || mTrackBinding.Size() != layout.NodeCount();

    if (stale)
    {
        PLU_PROFILE_SCOPE("Animation Track Binding");
        mTrackBinding.Clear();
        mTrackBinding.Resize(layout.NodeCount());
        for (UInt64 i = 0; i < layout.NodeCount(); ++i)
        {
            mTrackBinding[i] = Tracks.Find(layout.NodeName[i]);
        }
        mTrackBindingSkeletonUuid = skeletonUuid;
        mTrackBindingTrackCount = Tracks.Size();
        mTrackBindingBuilt = true;
    }
    return mTrackBinding;
}

void Plu::Animation::InvalidateTrackBinding() const
{
    mTrackBinding.Clear();
    mTrackBindingBuilt = false;
    mTrackBindingSkeletonUuid = 0;
    mTrackBindingTrackCount = 0;
}

double Plu::Animation::GetDurationSeconds() const
{
    if (FramesAmount <= 0 || FramesPerSecond <= 0.0f) return 0.0;
    return static_cast<double>(FramesAmount) / static_cast<double>(FramesPerSecond);
}

bool Plu::SampleAnimationPose(const Animation& animation, const Skeleton& skeleton,
                              double timeSeconds, bool loop, Pose& outPose)
{
    const SkeletonPoseLayout& layout = skeleton.GetPoseLayout();
    if (!layout.IsValid())
    {
        outPose.Clear();
        return false;
    }

    layout.MakeBindPose(outPose);

    const double duration = static_cast<double>(animation.FramesAmount);
    double ticks = timeSeconds * animation.FramesPerSecond;
    if (duration > 0.0)
    {
        if (loop)
        {
            ticks = std::fmod(ticks, duration);
            if (ticks < 0.0) ticks += duration;
        }
        else
        {
            ticks = std::clamp(ticks, 0.0, duration);
        }
    }

    const DynamicArray<const AnimationTrack*>& binding = animation.GetTrackBinding(skeleton);
    for (UInt64 i = 0; i < layout.NodeCount() && i < binding.Size(); ++i)
    {
        if (const AnimationTrack* track = binding[i])
        {
            outPose[i].Location = track->GetLocationAtTime(ticks);
            outPose[i].Rotation = track->GetRotationAtTime(ticks);
            outPose[i].Scale    = track->GetScaleAtTime(ticks);
        }
    }
    return true;
}
