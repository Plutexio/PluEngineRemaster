//
// Created by Plutex on 11/25/26.
//

#include "PluEngine/Gameplay/Debug/DebugDrawUtils.h"

namespace Plu
{
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