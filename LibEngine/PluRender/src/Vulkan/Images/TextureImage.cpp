//
// Created by Plutex on 11/22/26.
//

#include "PluEngine/Vulkan/Images/TextureImage.h"

#include "PluEngine/Vulkan/Buffers/Buffer.h"
#include "PluEngine/Vulkan/Utils/ImageUtils.h"

Plu::TextureImage::TextureImage(TUsePointer<ImageInfo> imageInfo, VkFormat imageFormat, VkImageTiling tiling, VkImageUsageFlags usage, PluVkContext context) : mContext(context) {
    if (!imageInfo) return;
    mImageInfo = imageInfo;
    mImageFormat = imageFormat;
    TOwningPointer<Buffer<UInt8> > stagingBuffer = CreateOwning<Buffer<UInt8> >(
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        imageInfo->GetMemorySize(),
        context
    );

    stagingBuffer->UploadData(imageInfo->GetPixels());

    VkImageCreateInfo imageCreateInfo{};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.extent.width = imageInfo->GetWidth();
    imageCreateInfo.extent.height = imageInfo->GetHeight();
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    //imageCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageCreateInfo.format = imageFormat;
    //imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.tiling = tiling;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    //imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCreateInfo.usage = usage;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocationCreateInfo{};
    allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkResult res = vmaCreateImage(context.Allocator, &imageCreateInfo, &allocationCreateInfo, &mImage, &mImageAllocation, nullptr);
    if (res != VK_SUCCESS) {
        PLU_CORE_ERROR("Error creating Image!");
        mImage = nullptr;
        mImageAllocation = nullptr;
        return;
    }

    TransitionImageLayout(*GetVulkanImage(), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mContext);
    CopyBufferToImage(stagingBuffer.GetRaw(), this, mContext);
    TransitionImageLayout(*GetVulkanImage(), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mContext);
}

Plu::TextureImage::~TextureImage() {
    if (!mImage) return;
    vmaDestroyImage(mContext.Allocator, mImage, mImageAllocation);
}

Plu::TUsePointer<Plu::ImageInfo> Plu::TextureImage::GetInfo() {
    return mImageInfo;
}

VkImage* Plu::TextureImage::GetVulkanImage() {
    return &mImage;
}

VkFormat Plu::TextureImage::GetImageFormat() {
    return mImageFormat;
}
