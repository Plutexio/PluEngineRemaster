//
// Created by Plutex on 11/22/26.
//

#ifndef VULKANTEST_TEXTUREIMAGE_H
#define VULKANTEST_TEXTUREIMAGE_H

#include "PluSTL/PluSTL_FWD.h"
#include "PluEngine/Vulkan/CoreVk.h"
#include "PluEngine/AssetTypes/Image/ImageInfo.h"

namespace Plu
{
    class TextureImage
    {
        VkImage mImage = nullptr;
        VmaAllocation mImageAllocation = nullptr;
        TUsePointer<ImageInfo> mImageInfo;
        PluVkContext mContext;

        VkFormat mImageFormat;
    public:
        TextureImage(TUsePointer<ImageInfo> imageInfo, VkFormat imageFormat, VkImageTiling tiling, VkImageUsageFlags usage, PluVkContext context);
        ~TextureImage();

        TUsePointer<ImageInfo> GetInfo();
        VkImage* GetVulkanImage();
        VkFormat GetImageFormat();
    };
}
#endif //VULKANTEST_TEXTUREIMAGE_H
