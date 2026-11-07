//
// Created by Plutex on 2026-03-09.
//

#include "../include/PluEngine/Physics/PhysicsPointRenderer.h"
#include <Jolt/Physics/Body/Body.h>
#include <glm/gtc/type_ptr.hpp>

using namespace Plu;

void JoltPointRenderer::BeginFrame()
{
    m_points.Clear();
}

void JoltPointRenderer::AddBody(const JPH::Body& body, const glm::vec3& color)
{
    auto tris  = ExtractTriangles(body.GetShape());
    auto world = JoltToGlm(body.GetCenterOfMassTransform());

    for (int i = 0; i < tris.Size(); i++)
    {
        glm::vec3 p = glm::vec3(world * glm::vec4(tris[i], 1.0f));
        m_points.PushBack({ p, color });
    }
}

void JoltPointRenderer::AddShape(const JPH::ShapeRefC& shape, const glm::mat4& transform, const glm::vec3& color)
{
    auto tris = ExtractTriangles(shape.GetPtr());

    for (int i = 0; i < tris.Size(); i++)
    {
        glm::vec3 p = glm::vec3(transform * glm::vec4(tris[i], 1.0f));
        m_points.PushBack({ p, color });
    }
}

void JoltPointRenderer::PackInto(DynamicArray<float>* out) const
{
    out->Reserve(out->Size() + m_points.Size() * 6);

    for (int i = 0; i < m_points.Size(); i++)
    {
        const Point& p = m_points[i];
        out->PushBack(p.pos.x);   out->PushBack(p.pos.y);   out->PushBack(p.pos.z);
        out->PushBack(p.color.r); out->PushBack(p.color.g); out->PushBack(p.color.b);
    }
}