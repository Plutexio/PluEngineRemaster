//
// Created by Plutex on 7/20/26.
//

#ifndef PLUENGINE_BONETRANSFORM_H
#define PLUENGINE_BONETRANSFORM_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluSTL/PluSTL_FWD.h"

namespace Plu
{
    // One bone's transform in decomposed form — the currency of the whole animation pipeline.
    // Animation keys are authored as Vec3/Quaternion, every graph node blends in this form, and
    // the conversion to Matrix4 happens exactly once at the end (SkeletonPoseLayout::BuildBonePalette).
    // Matrices are both more expensive to compose and impossible to interpolate meaningfully.
    //
    // Rotation is a quaternion, NOT Euler degrees like the Vec3 rotations elsewhere in the engine
    // (GetForwardVector, GameObject rotation, ...) — interpolating Euler angles is exactly what
    // this type exists to avoid.
    struct PLUASSETTYPES_API BoneTransform
    {
        Vec3       Location = Vec3(0.0f);
        Quaternion Rotation = Quaternion(1.0f, 0.0f, 0.0f, 0.0f); // (w, x, y, z) identity
        Vec3       Scale    = Vec3(1.0f);

        // translate(Location) * mat4_cast(Rotation) * scale(Scale).
        [[nodiscard]] Matrix4 ToMatrix() const;

        // Splits a matrix back into T/R/S. Shear is NOT representable and is silently dropped —
        // exact for bind poses and animation keys (both authored as TRS), lossy for a matrix that
        // was built with a skew. A mirroring matrix (negative determinant) folds the flip into
        // Scale.x so Rotation stays a real rotation.
        [[nodiscard]] static BoneTransform FromMatrix(const Matrix4& matrix);

        // Composes a parent-space child onto this (the parent): equivalent to
        // parent.ToMatrix() * child.ToMatrix(), without building either matrix.
        //
        // Caveat inherent to decomposed transforms (UE's FTransform has the same one): non-uniform
        // scale on a parent combined with a rotated child produces shear, which T/R/S cannot
        // represent, so the result is approximate in that case. Uniform scale is always exact.
        [[nodiscard]] BoneTransform Compose(const BoneTransform& child) const;

        // Transform that undoes this one. Undefined for a zero component in Scale.
        [[nodiscard]] BoneTransform Inverse() const;

        [[nodiscard]] Vec3 TransformPoint(const Vec3& point) const;

        // Renormalizes Rotation. Long chains of blends accumulate drift; worth calling before a
        // pose leaves the graph if it passed through many nodes.
        void NormalizeRotation();
    };

    // A full pose: one BoneTransform per node, indexed by SkeletonPoseLayout node index (so it
    // covers every node, bones and plain nodes alike). Local-space or skeleton-space depending on
    // what produced it — SkeletonPoseLayout::ComposeGlobals turns the former into the latter.
    using Pose = DynamicArray<BoneTransform>;

    // --- Blending -----------------------------------------------------------------------------

    // alpha 0 -> a, 1 -> b. Location and Scale lerp, Rotation slerps along the shortest arc.
    PLUASSETTYPES_API BoneTransform BlendTransforms(const BoneTransform& a, const BoneTransform& b, float alpha);

    // Applies an additive delta on top of base, scaled by alpha: rotation composed, location and
    // scale added. For additive layers — aim offsets, lean, recoil — where the delta was authored
    // relative to a reference pose rather than as a full pose.
    PLUASSETTYPES_API BoneTransform BlendTransformsAdditive(const BoneTransform& base, const BoneTransform& additive, float alpha);

    // Pose-wide versions. outPose is resized to match and may alias a or b.
    // Mismatched sizes are clamped to the shorter input.
    PLUASSETTYPES_API void BlendPoses(const Pose& a, const Pose& b, float alpha, Pose& outPose);
    PLUASSETTYPES_API void BlendPosesAdditive(const Pose& base, const Pose& additive, float alpha, Pose& outPose);

    // Per-bone alpha — bone masks and layered blending (e.g. upper body from one animation, legs
    // from another). boneWeights is indexed by node index; nodes past its end use defaultAlpha.
    PLUASSETTYPES_API void BlendPosesMasked(const Pose& a, const Pose& b, const DynamicArray<float>& boneWeights,
                                  float defaultAlpha, Pose& outPose);
}

#endif //PLUENGINE_BONETRANSFORM_H
