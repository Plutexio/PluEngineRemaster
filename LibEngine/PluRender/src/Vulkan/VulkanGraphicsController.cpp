//
// Created by Plutex on 11/25/26.
//

#include "PluEngine/Vulkan/VulkanGraphicsController.h"

#include "PluEngine/Core/ApplicationInfo.h"
#include "PluEngine/Platform/Platform.h"

void Plu::VulkanGraphicsController::CreateInstance()
{
    VkApplicationInfo appInfo = {};
    appInfo.pApplicationName = "Vulkan Test";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;

    VkInstanceCreateInfo createInfo = {};
    const DynamicArray<const char*> extensions = mAppInfo->AppPlatform->GetRequiredVulkanInstanceExtensions();

    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<UInt32>(extensions.Size());
    createInfo.ppEnabledExtensionNames = extensions.Data();

    VkResult res = vkCreateInstance(&createInfo, nullptr, &mPluVkContext.Instance);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Vk Instance Creation");
    }
}

void Plu::VulkanGraphicsController::Init()
{
    PLU_CORE_INFO("Initializing Vulkan Graphics API...");

    CreateInstance();

    PLU_CORE_INFO("Initialized Vulkan Graphics API");
}

void Plu::VulkanGraphicsController::Shutdown()
{

}
