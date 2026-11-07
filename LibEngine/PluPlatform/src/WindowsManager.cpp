//
// Created by Plutex on 8/5/26.
//

#include "PluEngine/Platform/WindowsManager.h"

#include "PluEngine/Core/ApplicationInfo.h"
#include "PluBase/Log.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Timer.h"

namespace Plu
{
    void WindowsManager::Initialize(ApplicationInfo *applicationInfo)
    {
        mApplicationInfo = applicationInfo;
    }

    void WindowsManager::RegisterWindow(const TOwningPointer<IWindow> &window)
    {
        if (!window) return;
        mWindows.PushBack(window);
    }

    TUsePointer<IWindow> WindowsManager::RequestNewWindow(const WindowProperties &properties)
    {
        TOwningPointer<IWindow> window = IWindow::PlutexCreateWindow(
            properties, mApplicationInfo->AppObjectManager, mApplicationInfo);
        if (!window) return nullptr;

        TUsePointer<IWindow> user = window;
        mPendingWindows.PushBack(window);
        PLU_CORE_INFO("Window {} requested ('{}')", window->GetWindowID(), properties.Title.CStr());
        return user;
    }

    void WindowsManager::RequestCloseWindow(UInt32 windowID)
    {
        // Window 0 is the application's own lifetime — Application::Run watches it and shuts the
        // engine down when it stops running, so the manager must not destroy it behind its back.
        if (windowID == 0) return;
        if (mWindowsToClose.Contains(windowID)) return;
        mWindowsToClose.PushBack(windowID);
        // Detach the context from the window at once so the SDL event pump stops feeding ImGui for
        // it; the context object itself dies later, once the render thread has let go (see below).
        if (TUsePointer<IWindow> window = GetWindow(windowID)) {
            window->SetImGuiContext(nullptr);
        }
        if (mImGuiOps) mImGuiOps.RequestTeardown(windowID);
    }

    bool WindowsManager::IsWindowClosing(UInt32 windowID) const
    {
        return mWindowsToClose.Contains(windowID);
    }

    UInt32 WindowsManager::GetWindowsAmount() const
    {
        return static_cast<UInt32>(mWindows.Size());
    }

    TUsePointer<IWindow> WindowsManager::GetWindow(UInt32 windowID) const
    {
        for (const TOwningPointer<IWindow>& window : mWindows) {
            if (window->GetWindowID() == windowID) return window;
        }
        // Requested but not created yet: RequestNewWindow promises the id resolves immediately, and
        // callers keying state off "does this window exist" must not see a gap of one frame.
        for (const TOwningPointer<IWindow>& window : mPendingWindows) {
            if (window->GetWindowID() == windowID) return window;
        }
        return nullptr;
    }

    TUsePointer<IWindow> WindowsManager::GetMainWindow() const
    {
        return GetWindow(0);
    }

    bool WindowsManager::IsAnyWindowFocused() const
    {
        for (const TOwningPointer<IWindow>& window : mWindows) {
            if (window->HasWindowFocus()) return true;
        }
        return false;
    }

    void WindowsManager::ProcessPendingWindows()
    {
        if (mPendingWindows.IsEmpty()) return;
        PLU_PROFILE_SCOPE("Process Pending Windows");

        for (TOwningPointer<IWindow>& window : mPendingWindows) {
            window->Init();
            mWindows.PushBack(window);
            if (window->GetWindowProperties().InitImGui) {
                if (mImGuiOps) mImGuiOps.CreateForWindow(window);
            }
            PLU_CORE_INFO("Window {} created", window->GetWindowID());
        }
        mPendingWindows.Clear();
    }

    void WindowsManager::ProcessClosingWindows()
    {
        if (mWindowsToClose.IsEmpty()) return;
        PLU_PROFILE_SCOPE("Process Closing Windows");

        // A window may only be destroyed once the render thread has acknowledged that it stopped
        // drawing into it and released its ImGui GL backend — until then the ids stay queued and
        // are retried next frame.
        DynamicArray<UInt32> stillClosing;
        for (UInt32 windowID : mWindowsToClose) {
            if (mImGuiOps && !mImGuiOps.IsTornDown(windowID)) {
                stillClosing.PushBack(windowID);
                continue;
            }
            if (mImGuiOps) mImGuiOps.DestroyForWindow(windowID);
            DestroyWindowNow(windowID);
        }
        mWindowsToClose = stillClosing;
    }

    void WindowsManager::DestroyWindowNow(UInt32 windowID)
    {
        for (size_t i = 0; i < mWindows.Size(); ++i) {
            if (mWindows[i]->GetWindowID() != windowID) continue;

            TOwningPointer<IWindow> window = mWindows[i];
            mWindows.RemoveAt(i);
            window->Shutdown();
            mApplicationInfo->AppObjectManager->DestroyObject(*window->GetEngineObjectHandle());
            PLU_CORE_INFO("Window {} destroyed", windowID);
            return;
        }
    }

    void WindowsManager::Shutdown()
    {
        mPendingWindows.Clear();
        mWindowsToClose.Clear();
        // Back to front, and never window 0 — Application owns that one. The render thread is
        // already joined here, so RenderingManager::Shutdown has handed every ImGui context back
        // to Main and the teardown needs no further handshake.
        for (size_t i = mWindows.Size(); i > 0; --i) {
            const UInt32 windowID = mWindows[i - 1]->GetWindowID();
            if (windowID == 0) continue;
            if (mImGuiOps) mImGuiOps.DestroyForWindow(windowID);
            DestroyWindowNow(windowID);
        }
    }
}
