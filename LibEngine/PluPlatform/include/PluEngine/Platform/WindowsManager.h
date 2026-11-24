//
// Created by Plutex on 8/5/26.
//

#ifndef PLUENGINE_WINDOWSMANAGER_H
#define PLUENGINE_WINDOWSMANAGER_H

#include <functional>

#include "PluEngine/Core.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "PluEngine/Platform/Window.h"
#include "WindowsManager.generated.h"

namespace Plu
{
    // Per-window ImGui context lifecycle. The contexts belong to whoever renders ImGui, but it is
    // the window manager that knows when a window appears or goes away — and it sits a layer below
    // the renderer, so it cannot name it. The renderer fills these in (none does right now).
    struct PLUPLATFORM_API ImGuiWindowContextOps
    {
        std::function<void(TUsePointer<IWindow>)> CreateForWindow;
        std::function<void(UInt32)> RequestTeardown;
        std::function<bool(UInt32)> IsTornDown;
        std::function<void(UInt32)> DestroyForWindow;

        explicit operator bool() const { return static_cast<bool>(CreateForWindow); }
    };

    // Owns every engine window. Creating and destroying an OS window is deferred to a well defined
    // point of the main-thread frame (ProcessPendingWindows / ProcessClosingWindows) because the
    // render thread walks the window list every frame — tearing a window down the moment a UI
    // callback asks for it would pull the platform handle (and its ImGui context) out from under it.
    //
    // Window 0 is the main window: it is registered here for lookup, but the manager never destroys
    // it — closing it ends Application::Run instead.
    PLU_CLASS()
    class PLUPLATFORM_API WindowsManager final : public EngineObject
    {
        REFLECTION_BODY_WINDOWSMANAGER()
    private:
        ApplicationInfo* mApplicationInfo = nullptr;
        ImGuiWindowContextOps mImGuiOps;

        DynamicArray<TOwningPointer<IWindow>> mWindows;
        // Created, not yet Init()ed — they get their platform window at the top of the next frame.
        DynamicArray<TOwningPointer<IWindow>> mPendingWindows;
        DynamicArray<UInt32> mWindowsToClose;

        // Unconditional teardown of one window; only ever called from the deferred paths below.
        void DestroyWindowNow(UInt32 windowID);

    public:
        void SetImGuiContextOps(const ImGuiWindowContextOps& ops) { mImGuiOps = ops; }
        void Initialize(ApplicationInfo* applicationInfo);

        // Registers an already created window (used for the main window, which Application creates
        // itself and Init()s on its own schedule).
        void RegisterWindow(const TOwningPointer<IWindow>& window);

        // Creates the window object right away — the returned pointer is usable for id/property
        // queries immediately — but the OS window only appears in ProcessPendingWindows().
        TUsePointer<IWindow> RequestNewWindow(const WindowProperties& properties);
        // Marks a window for destruction; it stops being drawn at once and is destroyed in
        // ProcessClosingWindows(). Requesting window 0 is ignored.
        void RequestCloseWindow(UInt32 windowID);
        // True between RequestCloseWindow and the window actually going away — the UI must stop
        // building frames for it immediately, the destruction itself lags by a frame or two.
        [[nodiscard]] bool IsWindowClosing(UInt32 windowID) const;

        [[nodiscard]] UInt32 GetWindowsAmount() const;
        [[nodiscard]] TUsePointer<IWindow> GetWindow(UInt32 windowID) const;
        [[nodiscard]] TUsePointer<IWindow> GetMainWindow() const;
        [[nodiscard]] const DynamicArray<TOwningPointer<IWindow>>& GetWindows() const { return mWindows; }

        // True when any engine window currently holds input focus.
        [[nodiscard]] bool IsAnyWindowFocused() const;

        // MAIN thread, start of frame: gives pending windows their platform window.
        void ProcessPendingWindows();
        // MAIN thread, end of frame: destroys the windows requested to close.
        void ProcessClosingWindows();

        // Destroys every secondary window; called during engine shutdown.
        void Shutdown();
    };
}

#endif //PLUENGINE_WINDOWSMANAGER_H
