//
// Created by Plutex on 11/26/26.
//

#ifndef PLUENGINE_WINDOWSPLATFORM_H
#define PLUENGINE_WINDOWSPLATFORM_H

#include "PluEngine/Core.h"

#ifdef PLU_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include "PluEngine/Platform/Platform.h"
#include "WindowsPlatform.generated.h"

namespace Plu
{
    PLU_CLASS()
    class PLUPLATFORM_API WindowsPlatform : public IPlatform
    {
        REFLECTION_BODY_WINDOWSPLATFORM()
    private:
        // Thread that pumps the messages, i.e. the one WaitForEvents() blocks and WakeEventLoop()
        // posts to. Captured in Init() rather than at static init so it does not depend on which
        // thread loaded the module.
        std::atomic<DWORD> mMessageThreadId{0};

        HWND mConsoleWindow = nullptr;
    public:
        explicit WindowsPlatform() = default;
        ~WindowsPlatform() override = default;

        void Init() override;
        void Shutdown() override;

        void PollEvents() override;
        void WaitForEvents(float timeoutSeconds) override;
        void WakeEventLoop() override;

        void SpawnConsoleWindow() override;

        DynamicArray<const char*> GetRequiredVulkanInstanceExtensions() override;
    };
}
#endif

#endif //PLUENGINE_WINDOWSPLATFORM_H
