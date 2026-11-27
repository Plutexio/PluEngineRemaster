//
// Created by Plutex on 11/17/26.
//

#include "PluEngine/Vulkan/Buffers/VertexBuffer.h"

VkVertexInputBindingDescription Plu::GetBindingDescriptionForVertex()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    return bindingDescription;
}

Plu::StaticArray<VkVertexInputAttributeDescription, 3> Plu::GetAttributeDescriptionsForVertex()
{
    Plu::StaticArray<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, Position);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, Color);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, UV);

    return attributeDescriptions;
}

Plu::VertexBuffer::VertexBuffer(DynamicArray<Vertex> *verticies, PluVkContext context)
: Buffer<Vertex>(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    0,
    verticies->Size(), context)
{
    TOwningPointer<Buffer> stagingBuffer = CreateOwning<Buffer>(
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        verticies->Size(), context
    );

    stagingBuffer->UploadData(verticies);
    stagingBuffer->CopyBuffer(this);
    stagingBuffer = nullptr;
}

Plu::VertexBuffer::~VertexBuffer() = default;