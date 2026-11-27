//
// Created by Plutex on 11/22/26.
//

#include "PluEngine/Vulkan/Images/TextureView.h"

#include "PluBase/Log.h"

Plu::TextureView::TextureView(TextureImage *textureImage, PluVkContext context) : TextureView(*textureImage->GetVulkanImage(), textureImage->GetImageFormat(), context) {}
Plu::TextureView::TextureView(VkImage image, VkFormat format, PluVkContext context) {
    mContext = context;

    VkImageViewCreateInfo viewCreateInfo{};
    viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCreateInfo.image = image;
    viewCreateInfo.format = format;
    viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCreateInfo.subresourceRange.baseMipLevel = 0;
    viewCreateInfo.subresourceRange.levelCount = 1;
    viewCreateInfo.subresourceRange.baseArrayLayer = 0;
    viewCreateInfo.subresourceRange.layerCount = 1;

    VkResult res = vkCreateImageView(context.LogicalDevice, &viewCreateInfo, nullptr, &mImageView);
    if (res != VK_SUCCESS) {
        PLU_CORE_ERROR("Error while creating ImageView!");
        mImageView = nullptr;
        return;
    }
}

Plu::TextureView::~TextureView() {
    if (!mImageView) return;
    vkDestroyImageView(mContext.LogicalDevice, mImageView, nullptr);
}

VkImageView* Plu::TextureView::GetVulkanImageView() {
    return &mImageView;
}
