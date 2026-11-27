//
// Created by Plutex on 11/25/26.
//

#include "PluEngine/Vulkan/VulkanGraphicsController.h"

#include "PluEngine/Core/ApplicationInfo.h"
#include "PluEngine/Platform/Platform.h"
#include "PluEngine/Platform/Window.h"
#include "PluBase/Types.h"

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

void Plu::VulkanGraphicsController::ChoosePhysicalDevice(PhysicalDevicePreference preference) {

    UInt32 physicalDeviceCount = 0;
    VkResult res = VK_NOT_READY;
    res = vkEnumeratePhysicalDevices(mPluVkContext.Instance, &physicalDeviceCount, nullptr);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Getting Number of physical devices");
    }
    DynamicArray<VkPhysicalDevice> physicalDevices;
    physicalDevices.Resize(physicalDeviceCount);
    res = vkEnumeratePhysicalDevices(mPluVkContext.Instance, &physicalDeviceCount, physicalDevices.Data());
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Getting physical devices");
    }

    Int32 chosenPhysicalDeviceIndex = -1;
    if (preference == PhysicalDevicePreference::DontCare) {
        goto finished;
    }
    for (UInt32 i = 0; i < physicalDeviceCount; i++) {
        VkPhysicalDeviceProperties2 deviceProperties{};
        deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkGetPhysicalDeviceProperties2(physicalDevices[i], &deviceProperties);
        switch (preference) {
            case PhysicalDevicePreference::DontCare:
                break;
            case PhysicalDevicePreference::Discrete:
                if (deviceProperties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    chosenPhysicalDeviceIndex = static_cast<Int32>(i);
                    goto finished;
                }
                break;
            case PhysicalDevicePreference::Integrated:
                if (deviceProperties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                    chosenPhysicalDeviceIndex = static_cast<Int32>(i);
                    goto finished;
                }
                break;
        }
    }
    finished:
    if (chosenPhysicalDeviceIndex == -1) {
        chosenPhysicalDeviceIndex = 0;
    }

    VkPhysicalDeviceProperties2 deviceProperties{};
    deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    vkGetPhysicalDeviceProperties2(physicalDevices[chosenPhysicalDeviceIndex], &deviceProperties);
    mPluVkContext.PhysicalDeviceProperties = deviceProperties.properties;
    mPluVkContext.PhysicalDevice = physicalDevices[chosenPhysicalDeviceIndex];
    PLU_CORE_INFO("Selected GPU: {}", mPluVkContext.PhysicalDeviceProperties.deviceName);
}

void Plu::VulkanGraphicsController::CreateLogicalDeviceAndQueues() {
    UInt32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(mPluVkContext.PhysicalDevice, &queueFamilyCount, nullptr);
    DynamicArray<VkQueueFamilyProperties> queueFamilies;
    queueFamilies.Resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(mPluVkContext.PhysicalDevice, &queueFamilyCount, queueFamilies.Data());

    UInt32 graphicsQueueIdx = -1;
    for (UInt32 i = 0; i < queueFamilies.Size(); ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsQueueIdx = i;
            break;
        }
    }

    if (!mAppInfo->AppPlatform->GetVulkanPresentationSupport(mPluVkContext.Instance,
        mPluVkContext.PhysicalDevice, graphicsQueueIdx)) {
        throw std::runtime_error("Presentation Settings");
        }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsQueueIdx;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    DynamicArray<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkPhysicalDeviceVulkan12Features enabledVk12Features = {};
    enabledVk12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    enabledVk12Features.descriptorIndexing = true;
    enabledVk12Features.shaderSampledImageArrayNonUniformIndexing = true;
    enabledVk12Features.descriptorBindingVariableDescriptorCount = true;
    enabledVk12Features.runtimeDescriptorArray = true;
    enabledVk12Features.bufferDeviceAddress = true;

    VkPhysicalDeviceVulkan13Features enabledVk13Features = {};
    enabledVk13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enabledVk13Features.pNext = &enabledVk12Features;
    enabledVk13Features.synchronization2 = true;
    enabledVk13Features.dynamicRendering = true;

    VkPhysicalDeviceFeatures enabledVkFeatures = {};
    enabledVkFeatures.samplerAnisotropy = true;
    enabledVkFeatures.shaderInt64 = VK_TRUE;

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pNext = &enabledVk13Features;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount = static_cast<UInt32>(deviceExtensions.Size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.Data();
    deviceCreateInfo.pEnabledFeatures = &enabledVkFeatures;

    VkResult res = VK_NOT_READY;
    res = vkCreateDevice(
        mPluVkContext.PhysicalDevice,
        &deviceCreateInfo,
        nullptr,
        &mPluVkContext.LogicalDevice
    );
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Logical Device Creation");
    }
    vkGetDeviceQueue(mPluVkContext.LogicalDevice, graphicsQueueIdx, 0, &mPluVkContext.GraphicsQueue);

}

void Plu::VulkanGraphicsController::CreateAllocator()
{
    VmaVulkanFunctions vkFuncs = {};
    vkFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vkFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorCreateInfo = {};
    allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorCreateInfo.instance = mPluVkContext.Instance;
    allocatorCreateInfo.physicalDevice = mPluVkContext.PhysicalDevice;
    allocatorCreateInfo.device = mPluVkContext.LogicalDevice;
    allocatorCreateInfo.pVulkanFunctions = &vkFuncs;

    VkResult res = VK_NOT_READY;
    res = vmaCreateAllocator(&allocatorCreateInfo, &mPluVkContext.Allocator);

    if (res != VK_SUCCESS) {
        throw std::runtime_error("Vma Allocator Creation");
    }
}

void Plu::VulkanGraphicsController::CreateSurface()
{
    if (!mAppInfo->AppPlatform->CreateVulkanSurface(mAppInfo->AppWindow, mPluVkContext.Instance, &mPluVkContext.Surface)) {
        throw std::runtime_error("Surface Creation");
    }
}

void Plu::VulkanGraphicsController::CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
    VkResult res = VK_NOT_READY;
    res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mPluVkContext.PhysicalDevice, mPluVkContext.Surface, &surfaceCapabilities);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Surface Capabilities");
    }

    VkExtent2D swapchainExtent = surfaceCapabilities.currentExtent;
    if (swapchainExtent.width == 0xFFFFFFFF) {
        swapchainExtent.width = mAppInfo->AppWindow->GetWidth();
        swapchainExtent.height = mAppInfo->AppWindow->GetHeight();
    }
    VkSwapchainCreateInfoKHR swapchainCreateInfo = {};
    swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface = mPluVkContext.Surface;
    swapchainCreateInfo.minImageCount = surfaceCapabilities.minImageCount;
    swapchainCreateInfo.imageFormat = SwapchainImageFormat;
    swapchainCreateInfo.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    swapchainCreateInfo.imageExtent = swapchainExtent;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;

    res = vkCreateSwapchainKHR(mPluVkContext.LogicalDevice, &swapchainCreateInfo, nullptr, &mPluVkContext.Swapchain);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Swapchain Creation");
    }
}

void Plu::VulkanGraphicsController::Init()
{
    PLU_CORE_INFO("Initializing Vulkan Graphics API...");

    CreateInstance();
    ChoosePhysicalDevice(PhysicalDevicePreference::Discrete);
    CreateLogicalDeviceAndQueues();
    CreateAllocator();
    CreateSurface();
    CreateSurface();

    PLU_CORE_INFO("Initialized Vulkan Graphics API");
}

void Plu::VulkanGraphicsController::Shutdown()
{

}
