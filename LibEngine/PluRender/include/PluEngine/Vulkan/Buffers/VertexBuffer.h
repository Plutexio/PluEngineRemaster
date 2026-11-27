//
// Created by Plutex on 11/17/26.
//

#ifndef VULKANTEST_VERTEXBUFFER_H
#define VULKANTEST_VERTEXBUFFER_H
#include "Buffer.h"
#include "PluBase/Types.h"
#include "PluSTL/PluSTL_FWD.h"

struct Vertex
{
    Vec2 Position;
    Vec3 Color;
    Vec2 UV;
};

namespace Plu
{
    VkVertexInputBindingDescription GetBindingDescriptionForVertex();
    StaticArray<VkVertexInputAttributeDescription, 3> GetAttributeDescriptionsForVertex();

    class VertexBuffer final : public Buffer<Vertex>
    {
    public:
        VertexBuffer(DynamicArray<Vertex>* verticies, PluVkContext context);
        ~VertexBuffer() override;
    };
}

#endif //VULKANTEST_VERTEXBUFFER_H
