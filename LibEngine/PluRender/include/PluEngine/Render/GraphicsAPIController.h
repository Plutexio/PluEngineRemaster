//
// Created by Plutex on 11/25/26.
//

#ifndef PLUENGINE_GRAPHICSAPICONTROLLER_H
#define PLUENGINE_GRAPHICSAPICONTROLLER_H

#include "PluEngine/Core/Objects/EngineObject.h"
#include "PluEngine/Core/Reflection/ClassPointer.h"
#include "GraphicsAPIController.generated.h"

namespace Plu
{
    PLU_CLASS(Abstract)
    class PLURENDER_API GraphicsApiController : public EngineObject
    {
        REFLECTION_BODY_GRAPHICSAPICONTROLLER()
    protected:
        ApplicationInfo* mAppInfo = nullptr;
    public:
        GraphicsApiController() = default;
        ~GraphicsApiController() override = default;

        static TOwningPointer<GraphicsApiController> CreateApiController(TClassPointer<GraphicsApiController> controllerClass, ApplicationInfo *appInfo);

        virtual void Init() = 0;
        virtual void Shutdown() = 0;
    };
}

#endif //PLUENGINE_GRAPHICSAPICONTROLLER_H
