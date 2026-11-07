//
// Created by Plutex on 10/11/26.
//

#ifndef PLUENGINE_SPRINGARMCOMPONENT_H
#define PLUENGINE_SPRINGARMCOMPONENT_H
#include "PluEngine/Core.h"
#include "PluEngine/Gameplay/WorldComponent.h"
#include "SpringArmComponent.generated.h"

namespace Plu
{
    PLU_CLASS(PyExport)
    class PLUGAMEPLAY_API SpringArmComponent final : public WorldComponent
    {
        REFLECTION_BODY_SPRINGARMCOMPONENT()
    public:
        SpringArmComponent() = default;
        virtual ~SpringArmComponent() override = default;

        bool HasAttachPoints() override;
        DynamicArray<String> GetAttachPoints() override;
        bool GetWorldMatrixForAttachPoint(String attachPoint, Matrix4 *outMatrix) override;

        void OnUpdate(float deltaTime) override;

        PLU_PROPERTY(PyExport)
        bool DebugDraw = false;
        PLU_PROPERTY(PyExport)
        float ArmLength = 1;
        PLU_PROPERTY(PyExport)
        bool UsePuppetControlRotation = false;
    };
}

#endif //PLUENGINE_SPRINGARMCOMPONENT_H
