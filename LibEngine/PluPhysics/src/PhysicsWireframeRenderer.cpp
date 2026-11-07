//
// Created by Plutex on 2026-03-09.
//

#include "../include/PluEngine/Physics/PhysicsWireframeRenderer.h"
#include <Jolt/Physics/Body/Body.h>
#include <glm/gtc/type_ptr.hpp>

using namespace Plu;

void JoltWireframeRenderer::BeginFrame()
{
    m_lines.Clear();
}

void JoltWireframeRenderer::AddBody(const JPH::Body& body, const glm::vec3& color)
{
    auto tris  = ExtractTriangles(body.GetShape());
    auto world = JoltToGlm(body.GetCenterOfMassTransform());

    for (int i = 0; i < tris.Size(); i += 3)
    {
        glm::vec3 a = glm::vec3(world * glm::vec4(tris[i+0], 1.0f));
        glm::vec3 b = glm::vec3(world * glm::vec4(tris[i+1], 1.0f));
        glm::vec3 c = glm::vec3(world * glm::vec4(tris[i+2], 1.0f));

        m_lines.PushBack({ a, b, color });
        m_lines.PushBack({ b, c, color });
        m_lines.PushBack({ c, a, color });
    }
}

void JoltWireframeRenderer::AddShape(const JPH::ShapeRefC& shape, const glm::mat4& transform, const glm::vec3& color)
{
    auto tris = ExtractTriangles(shape.GetPtr());

    for (int i = 0; i < tris.Size(); i += 3)
    {
        glm::vec3 a = glm::vec3(transform * glm::vec4(tris[i+0], 1.0f));
        glm::vec3 b = glm::vec3(transform * glm::vec4(tris[i+1], 1.0f));
        glm::vec3 c = glm::vec3(transform * glm::vec4(tris[i+2], 1.0f));

        m_lines.PushBack({ a, b, color });
        m_lines.PushBack({ b, c, color });
        m_lines.PushBack({ c, a, color });
    }
}

void JoltWireframeRenderer::PackInto(DynamicArray<float>* out) const
{
    out->Reserve(out->Size() + m_lines.Size() * 2 * 6);

    for (int i = 0; i < m_lines.Size(); i++)
    {
        const Line& l = m_lines[i];
        out->PushBack(l.a.x); out->PushBack(l.a.y); out->PushBack(l.a.z);
        out->PushBack(l.color.r); out->PushBack(l.color.g); out->PushBack(l.color.b);
        out->PushBack(l.b.x); out->PushBack(l.b.y); out->PushBack(l.b.z);
        out->PushBack(l.color.r); out->PushBack(l.color.g); out->PushBack(l.color.b);
    }
}