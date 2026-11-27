//
// Created by Plutex on 11/22/26.
//

#ifndef VULKANTEST_SINGLETIMCOMMANDS_H
#define VULKANTEST_SINGLETIMCOMMANDS_H

#include "PluEngine/Vulkan/CoreVk.h"

namespace Plu
{
    VkCommandBuffer BeginSingleTimeCommands(PluVkContext context);
    void EndSingleTimeCommands(PluVkContext context, VkCommandBuffer commandBuffer);
}

#endif //VULKANTEST_SINGLETIMCOMMANDS_H
