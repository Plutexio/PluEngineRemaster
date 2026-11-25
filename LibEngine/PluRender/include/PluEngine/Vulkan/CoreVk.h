//
// Created by Plutex on 11/25/26.
//

#ifndef PLUENGINE_COREVK_H
#define PLUENGINE_COREVK_H

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan.h>
#else
import vulkan_hpp
#endif

#include <vk_mem_alloc.h>

namespace Plu
{
    struct PluVkContext
    {
        VkInstance Instance;
        VkSurfaceKHR Surface;
        VkSwapchainKHR Swapchain;
        VkPipelineLayout PipelineLayout;
        VkPipeline GraphicsPipeline;
        VkDevice LogicalDevice;
        VkQueue GraphicsQueue;
        VkCommandPool CommandPool;
        VmaAllocator Allocator;
        VkPhysicalDeviceProperties PhysicalDeviceProperties;
    };
}


#endif //PLUENGINE_COREVK_H
