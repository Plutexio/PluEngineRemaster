//
// Created by Plutex on 11/25/26.
//

#ifndef PLUENGINE_VULKANINITIALIZER_H
#define PLUENGINE_VULKANINITIALIZER_H

#include "PluEngine/Render/GraphicsAPIController.h"
#include "PluEngine/Vulkan/CoreVk.h"
#include "VulkanGraphicsController.generated.h"

namespace Plu
{
    class TextureView;
    PLU_ENUM(PyNamespace=Plu)
    enum class PhysicalDevicePreference
    {
        DontCare,
        Discrete,
        Integrated
    };

    PLU_CLASS()
    class PLURENDER_API VulkanGraphicsController final : public GraphicsApiController
    {
        REFLECTION_BODY_VULKANGRAPHICSCONTROLLER()
    private:
        PluVkContext mPluVkContext = {};
        DynamicArray<TOwningPointer<TextureView>> mSwapchainImageViews;
        std::atomic_bool mIsReadyToRender = false;

        void CreateInstance();
        void ChoosePhysicalDevice(PhysicalDevicePreference preference);
        void CreateLogicalDeviceAndQueues();
        void CreateAllocator();
        void CreateSurface();

        void CreateSwapchain();
        void CreateSwapchainImageViews();
    public:
        VulkanGraphicsController() = default;
        ~VulkanGraphicsController() override = default;

        //Some constants
        static constexpr VkFormat SwapchainImageFormat = VK_FORMAT_B8G8R8A8_SRGB;

        bool IsReadyToRender() override;

        void Init() override;
        void Shutdown() override;
    };
}

#endif //PLUENGINE_VULKANINITIALIZER_H
