//
// Created by Plutex on 6/9/26.
//

#include "PluEngine/Render/RenderUtils.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "PluEngine/Timer.h"

#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/matrix_access.hpp"

namespace Plu
{
    void GetFrustumCornersWorldSpace(const Matrix4& proj, const Matrix4& view, Vec3 OutCorners[8])
    {
        const Matrix4 inv = glm::inverse(proj * view);

        int index = 0;
        for (int x = 0; x < 2; x++)
            for (int y = 0; y < 2; y++)
                for (int z = 0; z < 2; z++)
                {
                    Vec4 pt = inv * Vec4(2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
                    OutCorners[index++] = Vec3(pt) / pt.w;
                }
    }

    DynamicArray<Vec3> GetFrustumCornersWorldSpace(const Matrix4& proj, const Matrix4& view)
    {
        Vec3 corners[8];
        GetFrustumCornersWorldSpace(proj, view, corners);

        DynamicArray<Vec3> result;
        result.Reserve(8);
        for (const Vec3& c : corners)
            result.PushBack(c);
        return result;
    }
}
