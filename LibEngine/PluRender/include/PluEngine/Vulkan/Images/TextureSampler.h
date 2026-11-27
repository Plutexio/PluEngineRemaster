//
// Created by Plutex on 11/22/26.
//

#ifndef VULKANTEST_TEXTURESAMPLER_H
#define VULKANTEST_TEXTURESAMPLER_H

#include "PluSTL/PluSTL_FWD.h"
#include "PluEngine/Vulkan/CoreVk.h"

namespace Plu
{
    class TextureSampler
    {
        VkSampler mSampler;
        PluVkContext mContext;
    public:
        TextureSampler(PluVkContext context);
        ~TextureSampler();

        VkSampler* GetVulkanSampler();
    };
}

#endif //VULKANTEST_TEXTURESAMPLER_H
