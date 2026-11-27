//
// Created by Plutex on 11/17/26.
//

#ifndef VULKANTEST_INDEXBUFFER_H
#define VULKANTEST_INDEXBUFFER_H
#include "Buffer.h"

namespace Plu
{
    class IndexBuffer final : public Buffer<UInt16>
    {
    public:
        IndexBuffer(DynamicArray<UInt16>* indices, PluVkContext context);
        ~IndexBuffer() override;
    };
}
#endif //VULKANTEST_INDEXBUFFER_H
