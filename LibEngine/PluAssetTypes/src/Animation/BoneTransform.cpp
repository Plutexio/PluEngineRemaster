//
// Created by Plutex on 7/20/26.
//

#include "PluEngine/AssetTypes/Animation/BoneTransform.h"

#include "PluSTL/Array/Array.h"

// gtc only — glm/gtx/quaternion.hpp is an experimental extension and slerp/conjugate both live
// in ext/quaternion_common.hpp, which gtc/quaternion.hpp pulls in.
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

Matrix4 Plu::BoneTransform::ToMatrix() const
{
    // Rotation basis scaled per axis, translation dropped into the last column — the same result
    // as translate * mat4_cast * scale, minus two full 4x4 multiplies.
    Matrix4 result = glm::mat4_cast(Rotation);
    result[0] *= Scale.x;
    result[1] *= Scale.y;
    result[2] *= Scale.z;
    result[3] = Vec4(Location, 1.0f);
    return result;
}

Plu::BoneTransform Plu::BoneTransform::FromMatrix(const Matrix4& matrix)
{
    BoneTransform result;
    result.Location = Vec3(matrix[3]);

    const Vec3 basisX(matrix[0]);
    const Vec3 basisY(matrix[1]);
    const Vec3 basisZ(matrix[2]);
    result.Scale = Vec3(glm::length(basisX), glm::length(basisY), glm::length(basisZ));

    // Mirroring can't live in a quaternion, so park the flip in one scale axis and leave the
    // basis right-handed for quat_cast (which otherwise returns nonsense).
    if (glm::determinant(glm::mat3(matrix)) < 0.0f)
    {
        result.Scale.x = -result.Scale.x;
    }

    const glm::mat3 rotationBasis(
        result.Scale.x != 0.0f ? basisX / result.Scale.x : Vec3(1.0f, 0.0f, 0.0f),
        result.Scale.y != 0.0f ? basisY / result.Scale.y : Vec3(0.0f, 1.0f, 0.0f),
        result.Scale.z != 0.0f ? basisZ / result.Scale.z : Vec3(0.0f, 0.0f, 1.0f));

    result.Rotation = glm::normalize(glm::quat_cast(rotationBasis));
    return result;
}

Plu::BoneTransform Plu::BoneTransform::Compose(const BoneTransform& child) const
{
    BoneTransform result;
    result.Location = Location + (Rotation * (Scale * child.Location));
    result.Rotation = Rotation * child.Rotation;
    result.Scale    = Scale * child.Scale;
    return result;
}

Plu::BoneTransform Plu::BoneTransform::Inverse() const
{
    BoneTransform result;
    result.Scale    = Vec3(1.0f) / Scale;
    result.Rotation = glm::conjugate(Rotation);
    result.Location = result.Scale * (result.Rotation * -Location);
    return result;
}

Vec3 Plu::BoneTransform::TransformPoint(const Vec3& point) const
{
    return Location + (Rotation * (Scale * point));
}

void Plu::BoneTransform::NormalizeRotation()
{
    Rotation = glm::normalize(Rotation);
}

Plu::BoneTransform Plu::BlendTransforms(const BoneTransform& a, const BoneTransform& b, float alpha)
{
    BoneTransform result;
    result.Location = glm::mix(a.Location, b.Location, alpha);
    result.Rotation = glm::slerp(a.Rotation, b.Rotation, alpha); // shortest arc, handled by glm
    result.Scale    = glm::mix(a.Scale, b.Scale, alpha);
    return result;
}

Plu::BoneTransform Plu::BlendTransformsAdditive(const BoneTransform& base, const BoneTransform& additive, float alpha)
{
    BoneTransform result;
    result.Location = base.Location + additive.Location * alpha;
    // Scale the delta by slerping it away from identity, then stack it on the base rotation.
    result.Rotation = base.Rotation * glm::slerp(Quaternion(1.0f, 0.0f, 0.0f, 0.0f), additive.Rotation, alpha);
    result.Scale    = base.Scale + additive.Scale * alpha;
    return result;
}

namespace Plu
{
    // Pose-wide helpers all clamp to the shorter input rather than asserting: a graph node fed a
    // pose from a different skeleton should degrade, not take the process down.
    static UInt64 CommonPoseSize(const Pose& a, const Pose& b)
    {
        return a.Size() < b.Size() ? a.Size() : b.Size();
    }
}

void Plu::BlendPoses(const Pose& a, const Pose& b, float alpha, Pose& outPose)
{
    const UInt64 count = CommonPoseSize(a, b);
    if (outPose.Size() != count) outPose.Resize(count);
    for (UInt64 i = 0; i < count; ++i)
    {
        outPose[i] = BlendTransforms(a[i], b[i], alpha);
    }
}

void Plu::BlendPosesAdditive(const Pose& base, const Pose& additive, float alpha, Pose& outPose)
{
    const UInt64 count = CommonPoseSize(base, additive);
    if (outPose.Size() != count) outPose.Resize(count);
    for (UInt64 i = 0; i < count; ++i)
    {
        outPose[i] = BlendTransformsAdditive(base[i], additive[i], alpha);
    }
}

void Plu::BlendPosesMasked(const Pose& a, const Pose& b, const DynamicArray<float>& boneWeights,
                           float defaultAlpha, Pose& outPose)
{
    const UInt64 count = CommonPoseSize(a, b);
    if (outPose.Size() != count) outPose.Resize(count);
    for (UInt64 i = 0; i < count; ++i)
    {
        const float alpha = i < boneWeights.Size() ? boneWeights[i] : defaultAlpha;
        outPose[i] = BlendTransforms(a[i], b[i], alpha);
    }
}
