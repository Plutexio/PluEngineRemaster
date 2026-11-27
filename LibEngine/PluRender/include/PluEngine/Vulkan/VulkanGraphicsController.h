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

        void CreateInstance();
        void ChoosePhysicalDevice(PhysicalDevicePreference preference);
        void SelectQueues();
    public:
        VulkanGraphicsController() = default;
        ~VulkanGraphicsController() override = default;

        void Init() override;
        void Shutdown() override;
    };
}

#endif //PLUENGINE_VULKANINITIALIZER_H
