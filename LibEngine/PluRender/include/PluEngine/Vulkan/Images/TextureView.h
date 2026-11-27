//
// Created by Plutex on 11/22/26.
//

#ifndef VULKANTEST_TEXTUREVIEW_H
#define VULKANTEST_TEXTUREVIEW_H

#include "PluEngine/Vulkan/CoreVk.h"
#include "PluEngine/Vulkan/Images/TextureImage.h"

namespace Plu
{
    class TextureView
    {
        VkImageView mImageView;
        PluVkContext mContext;
    public:
        TextureView(TextureImage* textureImage, PluVkContext context);
        TextureView(VkImage image, VkFormat format, PluVkContext context);
        ~TextureView();

        VkImageView* GetVulkanImageView();
    };
}


#endif //VULKANTEST_TEXTUREVIEW_H
