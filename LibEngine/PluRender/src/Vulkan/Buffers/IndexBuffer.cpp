//
// Created by Plutex on 11/17/26.
//

#include "PluEngine/Vulkan/Buffers/IndexBuffer.h"

#include "PluSTL/Pointers/Casts.h"
#include "PluSTL/Pointers/TOwningPointer.h"

Plu::IndexBuffer::IndexBuffer(DynamicArray<UInt16> *indices, PluVkContext context)
: Buffer<UInt16>(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    0,
    indices->Size(), context)
{
    TOwningPointer<Buffer> stagingBuffer = CreateOwning<Buffer>(
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        indices->Size(), context
    );

    stagingBuffer->UploadData(indices);
    stagingBuffer->CopyBuffer(this);
    stagingBuffer = nullptr;
}

Plu::IndexBuffer::~IndexBuffer() = default;
