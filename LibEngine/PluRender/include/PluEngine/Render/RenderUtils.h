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
    void GetFrustumCornersWorldSpace(const Matrix4& proj, const Matrix4& view, Vec3 OutCorners[8]);
    DynamicArray<Vec3> GetFrustumCornersWorldSpace(const Matrix4& proj, const Matrix4& view);
}

#endif //PLUENGINE_RENDERUTILS_H
