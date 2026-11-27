//
// Created by Plutex on 11/16/26.
//

#ifndef VULKANTEST_BUFFER_H
#define VULKANTEST_BUFFER_H

#include "PluEngine/Vulkan/CoreVk.h"
#include "PluBase/Assert.h"
#include "PluBase/Base.h"
#include "PluSTL/Array/Array.h"
#include "PluSTL/Pointers/TUsePointer.h"

namespace Plu
{
    template<typename T>
    class Buffer
    {
    private:
        VkBuffer mBuffer = nullptr;
        VmaAllocation mBufferAllocation = nullptr;

        UInt64 mElementCount = 0;
    protected:
        PluVkContext mContext{};
    public:
        Buffer(VkBufferUsageFlags usage, VmaAllocationCreateFlags allocationFlags, UInt64 elementCount, PluVkContext context)
        {
            mContext = context;
            mElementCount = elementCount;
            PLU_CORE_ASSERT(mElementCount != 0, "Element count cannot be zero!")
            VkBufferCreateInfo bufferCreateInfo{};
            bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreateInfo.size = GetSizeInBytes();
            bufferCreateInfo.usage = usage;
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo bufferAllocCreateInfo{};
            bufferAllocCreateInfo.flags = allocationFlags;
            bufferAllocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

            VkResult result = vmaCreateBuffer(
                mContext.Allocator,
                &bufferCreateInfo,
                &bufferAllocCreateInfo,
                &mBuffer,
                &mBufferAllocation,
                nullptr
            );

            if (result != VK_SUCCESS) {
                PLU_CORE_ERROR("Error creating Buffer!");
                mBuffer = nullptr;
                return;
            }

        }
        virtual ~Buffer()
        {
            if (!mBuffer) return;
            vmaDestroyBuffer(mContext.Allocator, mBuffer, mBufferAllocation);
        }

        [[nodiscard]] UInt64 GetElementCount() const
        {
            return mElementCount;
        }

        [[nodiscard]] UInt64 GetSizeInBytes() const
        {
            return sizeof(T) * mElementCount;
        }

        [[nodiscard]] void* MapBuffer() const
        {
            void* mappedData = nullptr;
            vmaMapMemory(mContext.Allocator, mBufferAllocation, &mappedData);
            return mappedData;
        }

        void UnmapBuffer() const
        {
            vmaUnmapMemory(mContext.Allocator, mBufferAllocation);
        }

        void FlushBuffer() const
        {
            vmaFlushAllocation(mContext.Allocator, mBufferAllocation, 0, VK_WHOLE_SIZE);
        }

        VkBuffer* GetVulkanBuffer()
        {
            return &mBuffer;
        }

        void UploadData(DynamicArray<T>* data)
        {
            PLU_CORE_ASSERT(data->Size() <= mElementCount, "Buffer is too small for the supplied data!")
            vmaCopyMemoryToAllocation(mContext.Allocator, data->Data(), mBufferAllocation, 0, data->Size() * sizeof(T));
        }


        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        Buffer(Buffer&&) = delete;
        Buffer& operator=(Buffer&&) = delete;

        void CopyBuffer(Buffer* destination);
    };

    void CopyBufferImpl(VkBuffer src, VkBuffer dst, VkDeviceSize size, PluVkContext context);

    template<typename T>
    void Buffer<T>::CopyBuffer(Buffer* destination)
    {
        PLU_CORE_ASSERT(destination->GetSizeInBytes() >= GetSizeInBytes(), "Destination is smaller than Source buffer!")
        CopyBufferImpl(mBuffer, *destination->GetVulkanBuffer(), GetSizeInBytes(), mContext);
    }
}

#endif //VULKANTEST_BUFFER_H
