//
// Created by Plutex on 11/26/26.
//

#ifndef PLUENGINE_PLATFORM_H
#define PLUENGINE_PLATFORM_H

#include "PluSTL/PluSTL_FWD.h"
#include "PluSTL/Array/Array.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "PluEngine/Core.h"
#include "Platform.generated.h"
#include "PluEngine/Core/ApplicationInfo.h"

namespace Plu
{
    class EngineObjectManager;

    PLU_CLASS(Abstract)
    class PLUPLATFORM_API IPlatform : public EngineObject
    {
        REFLECTION_BODY_IPLATFORM()
    protected:
        ApplicationInfo* mApplicationInfo = nullptr;
    public:
        explicit IPlatform() = default;
        virtual ~IPlatform() = default;

        // Brings up the OS layer. Must run before the first window is created.
        virtual void Init() = 0;
        // Tears the OS layer down. Every window must be shut down by then.
        virtual void Shutdown() = 0;

        // Drains the OS event queue and routes each event to its window. Once per frame.
        virtual void PollEvents() = 0;
        // Power saving (FrameDemand.h). Blocks the calling (main) thread until an event is queued
        // or the timeout runs out; the event stays queued for PollEvents().
        virtual void WaitForEvents(float timeoutSeconds) = 0;
        // Any thread. Breaks WaitForEvents() out of its wait.
        virtual void WakeEventLoop() = 0;

        // Attaches a console the log output also goes to. No-op where the process already has one.
        virtual void SpawnConsoleWindow() = 0;

        virtual DynamicArray<const char*> GetRequiredVulkanInstanceExtensions() = 0;

        static TOwningPointer<IPlatform> PlutexCreatePlatform(const TUsePointer<EngineObjectManager>& objectManager,
                                                              ApplicationInfo* applicationInfo);
    };
}

#endif //PLUENGINE_PLATFORM_H
