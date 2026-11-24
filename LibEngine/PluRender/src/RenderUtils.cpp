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

    void AppendConeWireframe(DynamicArray<float>& OutLineVerts, const Vec3& Apex, const Vec3& Dir,
                             float Range, float HalfAngleRadians, const Vec3& Color, Int32 Segments,
                             float MaxHalfAngleRadians)
    {
        if (Range <= 0.0f || Segments < 3) return;

        const Vec3 axis = glm::normalize(Dir);
        const Vec3 reference = (glm::abs(glm::dot(axis, Vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
            ? Vec3(0.0f, 0.0f, 1.0f)
            : Vec3(0.0f, 1.0f, 0.0f);
        const Vec3 right = glm::normalize(glm::cross(axis, reference));
        const Vec3 up    = glm::cross(right, axis);

        const float halfAngle = glm::clamp(HalfAngleRadians, 0.0f, glm::clamp(MaxHalfAngleRadians, 0.0f, glm::pi<float>()));
        const Vec3  baseCenter = Apex + axis * (Range * std::cos(halfAngle));
        const float baseRadius = Range * std::sin(halfAngle);

        auto pushVertex = [&](const Vec3& position) {
            OutLineVerts.PushBack(position.x);
            OutLineVerts.PushBack(position.y);
            OutLineVerts.PushBack(position.z);
            OutLineVerts.PushBack(Color.r);
            OutLineVerts.PushBack(Color.g);
            OutLineVerts.PushBack(Color.b);
        };

        OutLineVerts.Reserve(OutLineVerts.Size() + static_cast<UInt32>(Segments) * 4 * 6);

        const float step = glm::two_pi<float>() / static_cast<float>(Segments);
        Vec3 previous = baseCenter + right * baseRadius;
        for (Int32 s = 1; s <= Segments; s++)
        {
            const float angle = step * static_cast<float>(s);
            const Vec3 current = baseCenter + (right * std::cos(angle) + up * std::sin(angle)) * baseRadius;

            pushVertex(previous);
            pushVertex(current);

            if ((s % std::max(Segments / 4, 1)) == 0)
            {
                pushVertex(Apex);
                pushVertex(current);
            }

            previous = current;
        }
    }

    void AppendSphereWireframe(DynamicArray<float>& OutLineVerts, const Vec3& Center, float Radius,
                               const Vec3& Color, Int32 Segments)
    {
        if (Radius <= 0.0f || Segments < 3) return;

        auto pushVertex = [&](const Vec3& position) {
            OutLineVerts.PushBack(position.x);
            OutLineVerts.PushBack(position.y);
            OutLineVerts.PushBack(position.z);
            OutLineVerts.PushBack(Color.r);
            OutLineVerts.PushBack(Color.g);
            OutLineVerts.PushBack(Color.b);
        };

        OutLineVerts.Reserve(OutLineVerts.Size() + static_cast<UInt32>(Segments) * 3 * 2 * 6);

        const Vec3 axes[3][2] = {
            {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)},
            {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)},
            {Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)},
        };
        const float step = glm::two_pi<float>() / static_cast<float>(Segments);
        for (const auto& plane : axes)
        {
            Vec3 previous = Center + plane[0] * Radius;
            for (Int32 s = 1; s <= Segments; s++)
            {
                const float angle = step * static_cast<float>(s);
                const Vec3 current = Center + (plane[0] * std::cos(angle) + plane[1] * std::sin(angle)) * Radius;
                pushVertex(previous);
                pushVertex(current);
                previous = current;
            }
        }
    }
}
