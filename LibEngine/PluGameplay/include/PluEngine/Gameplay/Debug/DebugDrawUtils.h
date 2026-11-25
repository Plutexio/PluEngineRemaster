//
// Created by Plutex on 11/25/26.
//

#ifndef PLUENGINE_DEBUGDRAWUTILS_H
#define PLUENGINE_DEBUGDRAWUTILS_H

#include "PluEngine/Core.h"
#include "PluBase/Types.h"
#include "PluSTL/PluSTL_FWD.h"

namespace Plu
{
    PLUGAMEPLAY_API void AppendConeWireframe(DynamicArray<float>& OutLineVerts, const Vec3& Apex, const Vec3& Dir,
                                     float Range, float HalfAngleRadians, const Vec3& Color, Int32 Segments = 24,
                                     float MaxHalfAngleRadians = glm::half_pi<float>() - 0.01f);
    PLUGAMEPLAY_API void AppendSphereWireframe(DynamicArray<float>& OutLineVerts, const Vec3& Center, float Radius,
                                       const Vec3& Color, Int32 Segments = 32);
}

#endif //PLUENGINE_DEBUGDRAWUTILS_H
