//
// Created by Plutex on 11/26/26.
//

#include "PluEngine/Platforms/Windows/WindowsPlatform.h"

#ifdef PLU_PLATFORM_WINDOWS

#include <cstdio>
#include <iostream>

#include "PluBase/Log.h"
#include "PluEngine/FrameDemand.h"
#include "PluEngine/Platform/Window.h"
#include "PluEngine/Timer.h"
#include "spdlog/sinks/wincolor_sink.h"

namespace Plu
{
    void WindowsPlatform::Init()
    {
        mMessageThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
    }

    void WindowsPlatform::Shutdown()
    {
        mMessageThreadId.store(0, std::memory_order_relaxed);
    }

    void WindowsPlatform::PollEvents()
    {
        PLU_PROFILE_SCOPE("WindowsPlatform::PollEvents");
        MSG msg = {};
        // hWnd = nullptr (not a window handle): cross-process OLE drag&drop delivers IDropTarget
        // calls as RPC messages routed through a hidden window OleInitialize creates on this thread,
        // not through our windows. Filtering PeekMessage to one window starves that hidden window's
        // queue, so DragEnter/DragOver/Drop arrive late or never - this is what caused the drop
        // overlay to flicker and file drops to silently do nothing. Every engine window gets its
        // messages through WindowProc from DispatchMessage.
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            // Any OS message may change what is on screen. A bare pointer move usually does not, so
            // it only asks for probe frames (the UI decides whether anything changed). WM_NULL is
            // the wake message: whoever posted it has already stated what it needs.
            if (msg.message == WM_MOUSEMOVE || msg.message == WM_NCMOUSEMOVE) RequestProbe();
            else if (msg.message != WM_NULL) RequestRedraw();
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    void WindowsPlatform::WaitForEvents(float timeoutSeconds)
    {
        // MWMO_INPUTAVAILABLE: also return for input that was already queued before the call, not
        // only for input arriving during it.
        MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(timeoutSeconds * 1000.0f), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }

    void WindowsPlatform::WakeEventLoop()
    {
        const DWORD threadId = mMessageThreadId.load(std::memory_order_relaxed);
        if (threadId != 0) PostThreadMessage(threadId, WM_NULL, 0, 0);
    }

    DynamicArray<const char*> WindowsPlatform::GetRequiredVulkanInstanceExtensions()
    {
        // Spelled out rather than VK_KHR_*_EXTENSION_NAME so the platform layer needs no Vulkan headers.
        DynamicArray<const char*> extensions;
        extensions.PushBack("VK_KHR_surface");
        extensions.PushBack("VK_KHR_win32_surface");
        return extensions;
    }

    bool WindowsPlatform::GetVulkanPresentationSupport(VkInstance instance, VkPhysicalDevice physicalDevice,
                                                       UInt32 queueFamilyIndex)
    {
        // The Win32 query takes no instance. The loader (vulkan-1.dll, already loaded by PluRender)
        // exports it directly, so it is looked up there instead of linking Vulkan into this module.
        (void)instance;
        using PresentationSupportFn = UInt32 (__stdcall*)(VkPhysicalDevice, UInt32);
        static const auto presentationSupport = []() -> PresentationSupportFn {
            HMODULE loader = GetModuleHandleW(L"vulkan-1.dll");
            if (!loader) {
                PLU_CORE_ERROR("GetVulkanPresentationSupport: vulkan-1.dll is not loaded");
                return nullptr;
            }
            return reinterpret_cast<PresentationSupportFn>(
                GetProcAddress(loader, "vkGetPhysicalDeviceWin32PresentationSupportKHR"));
        }();
        if (!presentationSupport) {
            return false;
        }
        return presentationSupport(physicalDevice, queueFamilyIndex) != 0;
    }

    bool WindowsPlatform::CreateVulkanSurface(const TUsePointer<IWindow>& window, VkInstance instance,
                                              VkSurfaceKHR* outSurface)
    {
        if (!window || !outSurface) {
            PLU_CORE_ERROR("CreateVulkanSurface: no window or output surface");
            return false;
        }

        // Mirrors VkWin32SurfaceCreateInfoKHR, so the platform layer needs no Vulkan headers.
        struct Win32SurfaceCreateInfo
        {
            Int32 sType; // VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR
            const void* pNext;
            UInt32 flags;
            HINSTANCE hinstance;
            HWND hwnd;
        };
        constexpr Int32 kWin32SurfaceCreateInfoType = 1000009000;

        using VoidFn = void (__stdcall*)();
        using GetInstanceProcAddrFn = VoidFn (__stdcall*)(VkInstance, const char*);
        using CreateWin32SurfaceFn = Int32 (__stdcall*)(VkInstance, const Win32SurfaceCreateInfo*,
                                                        const void* /*pAllocator*/, VkSurfaceKHR*);

        // vkCreateWin32SurfaceKHR is an instance function: resolved through the loader's
        // vkGetInstanceProcAddr (vulkan-1.dll, already loaded by PluRender) for this instance.
        HMODULE loader = GetModuleHandleW(L"vulkan-1.dll");
        if (!loader) {
            PLU_CORE_ERROR("CreateVulkanSurface: vulkan-1.dll is not loaded");
            return false;
        }
        const auto getInstanceProcAddr = reinterpret_cast<GetInstanceProcAddrFn>(
            GetProcAddress(loader, "vkGetInstanceProcAddr"));
        const auto createSurface = getInstanceProcAddr
            ? reinterpret_cast<CreateWin32SurfaceFn>(getInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"))
            : nullptr;
        if (!createSurface) {
            PLU_CORE_ERROR("CreateVulkanSurface: vkCreateWin32SurfaceKHR not available "
                           "(is VK_KHR_win32_surface enabled on the instance?)");
            return false;
        }

        Win32SurfaceCreateInfo createInfo = {};
        createInfo.sType = kWin32SurfaceCreateInfoType;
        createInfo.hinstance = GetModuleHandleW(nullptr);
        createInfo.hwnd = static_cast<HWND>(window->GetWindowHandle());

        const Int32 result = createSurface(instance, &createInfo, nullptr, outSurface);
        if (result != 0) { // VK_SUCCESS
            PLU_CORE_ERROR("vkCreateWin32SurfaceKHR failed: VkResult {}", result);
            return false;
        }
        return true;
    }

    void WindowsPlatform::SpawnConsoleWindow()
    {
        if (!AllocConsole())
            return; // the console already exists, or allocation failed

        FILE* fp_out, * fp_err, * fp_in;
        freopen_s(&fp_out, "CONOUT$", "w", stdout);
        freopen_s(&fp_err, "CONOUT$", "w", stderr);
        freopen_s(&fp_in, "CONIN$", "r", stdin);

        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        std::ios::sync_with_stdio(true);

        mConsoleWindow = GetConsoleWindow();
        if (mConsoleWindow)
            SetWindowTextW(mConsoleWindow, L"Editor Console");

        auto stdout_sink = std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>();
        stdout_sink->set_level(spdlog::level::trace);

        if (auto& coreLogger = Log::GetCoreLogger())
            coreLogger->sinks().push_back(stdout_sink);
        if (auto& clientLogger = Log::GetClientLogger())
            clientLogger->sinks().push_back(stdout_sink);

        PLU_CORE_INFO("Console Allocated");
    }
}

#endif
