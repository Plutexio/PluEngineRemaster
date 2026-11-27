//
// Created by Plutex on 11/22/26.
//

#include "PluEngine/Vulkan/Images/TextureSampler.h"

#include "PluBase/Log.h"

Plu::TextureSampler::TextureSampler(PluVkContext context) {
    mContext = context;
    VkSamplerCreateInfo samplerCreateInfo{};
    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;

    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerCreateInfo.anisotropyEnable = VK_TRUE;
    samplerCreateInfo.maxAnisotropy = mContext.PhysicalDeviceProperties.limits.maxSamplerAnisotropy;

    samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
    samplerCreateInfo.compareEnable = VK_FALSE;
    samplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCreateInfo.mipLodBias = 0.0f;
    samplerCreateInfo.minLod = 0.0f;
    samplerCreateInfo.maxLod = 0.0f;

    VkResult res = vkCreateSampler(mContext.LogicalDevice, &samplerCreateInfo, nullptr, &mSampler);
    if (res != VK_SUCCESS) {
        PLU_CORE_ERROR("Error creating TextureSampler!");
        mSampler = nullptr;
        return;
    }
}
Plu::TextureSampler::~TextureSampler() {
    if (!mSampler) return;
    vkDestroySampler(mContext.LogicalDevice, mSampler, nullptr);
}

VkSampler* Plu::TextureSampler::GetVulkanSampler()
{
    return &mSampler;
}
