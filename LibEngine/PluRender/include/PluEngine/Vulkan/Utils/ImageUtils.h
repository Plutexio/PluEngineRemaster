//
// Created by Plutex on 11/22/26.
//

#ifndef VULKANTEST_IMAGEUTILS_H
#define VULKANTEST_IMAGEUTILS_H

#include "PluEngine/Vulkan/CoreVk.h"
#include "PluEngine/Vulkan/Buffers/Buffer.h"
#include "PluEngine/Vulkan/Images/TextureImage.h"

namespace Plu
{
    void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, PluVkContext context);
    void CopyBufferToImage(Buffer<UInt8>* buffer, TextureImage* image, PluVkContext context);
}

#endif //VULKANTEST_IMAGEUTILS_H
