//
// Created by Plutex on 11/17/26.
//

#include "PluEngine/Vulkan/Buffers/Buffer.h"
#include "PluEngine/Vulkan/Utils/SingleTimCommands.h"

void Plu::CopyBufferImpl(VkBuffer src, VkBuffer dst, VkDeviceSize size, PluVkContext context)
{
    VkCommandBuffer commandBuffer =  BeginSingleTimeCommands(context);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, src, dst, 1, &copyRegion);

    EndSingleTimeCommands(context, commandBuffer);
}
