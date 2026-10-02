//
// Created by Plutex on 12/30/25.
//

#ifndef PLUENGINE_GLFWWINDOW_H
#define PLUENGINE_GLFWWINDOW_H

#include "PluEngine/Core.h"

#ifdef PLU_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <PluSTL_FWD.h>
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Platform/Window.h"
#include "WindowsWindow.generated.h"

struct IDropTarget;

namespace Plu
{
    PLU_CLASS()
    class WindowsWindow : public IWindow
    {
        REFLECTION_BODY_WINDOWSWINDOW()
    private:
        HWND mHandle;
        HDC mHDC;
        HGLRC mGLContext;
        HICON mhIcon;

        HWND mConsoleWindow;

        bool mHasFocus = false;
        bool  mVSyncEnabled = true;
        bool mIsRunning;

        IDropTarget* mDropTarget = nullptr;

        FullscreenType mFullscreenType = FullscreenType::Windowed;
        // Styl i geometria sprzed pierwszego wejścia w fullscreen — do przywrócenia w Windowed.
        LONG mWindowedStyle = 0;
        WINDOWPLACEMENT mWindowedPlacement = { sizeof(WINDOWPLACEMENT) };

        void DestroyOpenGL();
        HGLRC InitOpenGL(HWND hWnd);
        bool SetupPixelFormat(HDC hdc);

        friend LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
        friend class WinDropTarget;
    protected:
        void Close() override;
    public:
        explicit WindowsWindow();
        virtual ~WindowsWindow();

        void Init() override;
        void OnUpdate(float deltaTime) override;
        // Power saving (FrameDemand.h). Blocks the calling (main) thread until a message is queued
        // or the timeout runs out; the message stays queued for OnUpdate().
        static void WaitForEvents(float timeoutSeconds);
        // Any thread. Breaks WaitForEvents() out of its wait.
        static void WakeEventLoop();
        void Shutdown() override;

        bool IsRunning() override;

        int GetHeight() override;
        int GetWidth() override;

        IVec2 GetWindowPosition() override;
        void SetWindowPosition(IVec2 position) override;

        bool IsWindowMaximized() override;
        bool IsWindowMinimized() override;
        void Maximize() override;
        void Minimize() override;

        void MakeFullscreen(FullscreenType type, IVec2 resolution = IVec2(0, 0)) override;
        FullscreenType GetFullscreenType() const override;
        DynamicArray<DisplayMode> GetSupportedDisplayModes() override;
        IVec2 GetDesktopResolution() override;

        void* GetWindowHandle() override;

        bool HasWindowFocus() override;

        bool IsVSyncEnabled() override;
        void SetVSyncEnabled(bool enable) override;
        void ApplySwapInterval(bool vsync) override;

        void SetWindowTitle(String title) override;

        void* GetGLContext() override;
        void SpawnConsoleWindow();
        void MakeGLContextCurrent() override;
        void ReleaseGLContext() override;
        void SwapBuffer() override;

        void SetCursorVisibility(bool visible) override;
        IVec2 GetCursorPosition() override;
        void SetCursorPosition(IVec2 pos) override;
    };
}
#endif

#endif //PLUENGINE_GLFWWINDOW_H