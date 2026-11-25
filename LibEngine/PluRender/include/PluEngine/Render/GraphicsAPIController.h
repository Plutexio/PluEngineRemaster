//
// Created by Plutex on 11/25/26.
//

#ifndef PLUENGINE_GRAPHICSAPICONTROLLER_H
#define PLUENGINE_GRAPHICSAPICONTROLLER_H

#include "PluEngine/Core/Objects/EngineObject.h"
#include "GraphicsAPIController.generated.h"

namespace Plu
{
    PLU_CLASS()
    class PLURENDER_API GraphicsApiController : public EngineObject
    {
        REFLECTION_BODY_GRAPHICSAPICONTROLLER()
    public:
        GraphicsApiController() = default;
        ~GraphicsApiController() override = default;


        virtual void Init() = 0;
        virtual void Shutdown() = 0;
    };
}

#endif //PLUENGINE_GRAPHICSAPICONTROLLER_H
