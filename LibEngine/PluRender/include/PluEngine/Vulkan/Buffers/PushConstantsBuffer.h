//
// Created by Plutex on 11/19/26.
//

#ifndef VULKANTEST_PUSHCONSTANTSBUFFER_H
#define VULKANTEST_PUSHCONSTANTSBUFFER_H
#include "Buffer.h"
#include "PluEngine/Vulkan/CoreVk.h"

namespace Plu
{
    template<typename T>
    class PushConstantsBuffer : Plu::Buffer<T>
    {
        VkDeviceAddress mDeviceAddress;
    public:
        PushConstantsBuffer(PluVkContext context)
        : Buffer<T>(
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
            1,
            context
        )
        {
            VkBufferDeviceAddressInfo bufferDeviceAddressInfo{};
            bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            bufferDeviceAddressInfo.buffer = *this->GetVulkanBuffer();
            mDeviceAddress = vkGetBufferDeviceAddress(this->mContext.LogicalDevice, &bufferDeviceAddressInfo);
        }
        ~PushConstantsBuffer() override = default;

        [[nodiscard]] VkDeviceAddress* GetDeviceAddress() {
            return &mDeviceAddress;
        }

        void CopyToBuffer(T* data) {
            void* mapped = this->MapBuffer();
            memcpy(mapped, data, this->GetSizeInBytes());
            this->UnmapBuffer();
        }
    };
}

#endif //VULKANTEST_PUSHCONSTANTSBUFFER_H
