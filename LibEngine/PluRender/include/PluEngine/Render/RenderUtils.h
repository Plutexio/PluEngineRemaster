//
// Created by Plutex on 6/9/26.
//

#ifndef PLUENGINE_RENDERUTILS_H
#define PLUENGINE_RENDERUTILS_H
#include <cstddef>

#include <glm/gtc/constants.hpp>

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"

namespace Plu
{
    constexpr float kCameraNearClip = 0.1f;
    constexpr float kCameraFarClip  = 100000.0f;

    void GetFrustumCornersWorldSpace(const Matrix4& proj, const Matrix4& view, Vec3 OutCorners[8]);
    DynamicArray<Vec3> GetFrustumCornersWorldSpace(const Matrix4& proj, const Matrix4& view);

    PLURENDER_API void AppendConeWireframe(DynamicArray<float>& OutLineVerts, const Vec3& Apex, const Vec3& Dir,
                                     float Range, float HalfAngleRadians, const Vec3& Color, Int32 Segments = 24,
                                     float MaxHalfAngleRadians = glm::half_pi<float>() - 0.01f);
    PLURENDER_API void AppendSphereWireframe(DynamicArray<float>& OutLineVerts, const Vec3& Center, float Radius,
                                       const Vec3& Color, Int32 Segments = 32);
}

#endif //PLUENGINE_RENDERUTILS_H
