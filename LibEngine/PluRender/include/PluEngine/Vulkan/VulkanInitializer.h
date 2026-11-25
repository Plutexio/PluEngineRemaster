//
// Created by Plutex on 11/25/26.
//

#ifndef PLUENGINE_VULKANINITIALIZER_H
#define PLUENGINE_VULKANINITIALIZER_H

#include "PluEngine/Render/GraphicsAPIController.h"
#include "VulkanInitializer.generated.h"

namespace Plu
{
    PLU_CLASS()
    class PLURENDER_API VulkanGraphicsController final : public GraphicsApiController
    {
        REFLECTION_BODY_VULKANGRAPHICSCONTROLLER()
    public:
        VulkanGraphicsController() = default;
        ~VulkanGraphicsController() override = default;

        void Init() override;
        void Shutdown() override;
    };
}

#endif //PLUENGINE_VULKANINITIALIZER_H
