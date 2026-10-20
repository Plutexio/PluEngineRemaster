//
// Created by Plutex on 12/30/25.
//

#ifndef PLUENGINE_WINDOW_H
#define PLUENGINE_WINDOW_H

#include "PluSTL/PluSTL_FWD.h"
#include "PluSTL/Array/Array.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "PluEngine/Core.h"
#include "Window.generated.h"
#include "PluEngine/Core/ApplicationInfo.h"
#include "PluEngine/PluTypes.h"

namespace Plu
{
    class Application;
    PLU_ENUM(PyExport, PyNamespace=Plu)
    enum class FullscreenType
    {
        // Zwykłe okno z ramką (albo bez, jeśli WindowProperties::Borderless) i przywróconą pozycją.
        Windowed,
        // Wyłączny fullscreen: zmienia tryb wideo monitora na żądaną rozdzielczość.
        Fullscreen,
        // Bezramkowe okno rozciągnięte na cały monitor, bez zmiany trybu wideo.
        BorderlessWindow
    };

    // Tryb wideo monitora. RefreshRate w Hz; 0 = nieznany.
    struct DisplayMode
    {
        int Width = 0;
        int Height = 0;
        float RefreshRate = 0.0f;
    };

    struct WindowProperties
    {
        String Title;
        int Width;
        int Height;
        // When set, WindowsManager creates an ImGui context for the window.
        bool InitImGui;
        bool Borderless;
        bool Resizable;
        // Top-left corner in desktop coordinates; {-1,-1} centers the window on its display.
        IVec2 Position;

        WindowProperties() : Title("New Window"), Width(1000), Height(720), InitImGui(false),
                             Borderless(false), Resizable(true), Position(-1, -1) {}
        WindowProperties(const String &title) : WindowProperties()
        {
            Title = title;
        }
    };

    class EngineObjectManager;

    PLU_CLASS(Abstract)
    class PLUPLATFORM_API IWindow : public EngineObject
    {
        REFLECTION_BODY_IWINDOW()
    protected:
        WindowProperties mProperties;
        ApplicationInfo* mApplicationInfo;
        ImGuiContext* mImGuiContext = nullptr;

        // Engine-side window id: a monotonic counter handed out by PlutexCreateWindow, unrelated to
        // any platform id (SDL has its own id space — see SDLWindow::GetSDLWindowID). The main
        // window is always 0, and everything above the platform layer addresses windows by this id.
        UInt32 mWindowID = 0;

        // Friend class, not the member function: naming Application::DispatchWindowClose would
        // need the complete type, and Application lives in the layer above this one.
        friend class Application;
        virtual void Close() = 0;
    public:
        explicit IWindow() = default;
        virtual ~IWindow() = default;

        void SetWindowProperties(const WindowProperties& properties);
        [[nodiscard]] const WindowProperties& GetWindowProperties() const { return mProperties; }

        [[nodiscard]] UInt32 GetWindowID() const { return mWindowID; }

        virtual void Init() = 0;
        virtual void OnUpdate(float deltaTime) = 0;
        virtual void Shutdown() = 0;

        virtual bool IsRunning() = 0;

        virtual int GetWidth() = 0;
        virtual int GetHeight() = 0;

        // Top-left corner of the window in desktop coordinates. Used to persist the editor layout.
        virtual IVec2 GetWindowPosition() = 0;
        virtual void SetWindowPosition(IVec2 position) = 0;

        virtual void Minimize() = 0;
        virtual void Maximize() = 0;
        virtual bool IsWindowMinimized() = 0;
        virtual bool IsWindowMaximized() = 0;

        // Przełącza tryb wyświetlania. `resolution` dotyczy tylko FullscreenType::Fullscreen —
        // {0,0} oznacza natywną rozdzielczość pulpitu. Windowed przywraca zapamiętaną pozycję
        // i rozmiar sprzed pierwszego przejścia w fullscreen. Wołać z main threadu.
        virtual void MakeFullscreen(FullscreenType type, IVec2 resolution = IVec2(0, 0)) = 0;
        virtual FullscreenType GetFullscreenType() const = 0;
        // Tryby wideo monitora, na którym stoi okno, posortowane rosnąco (szerokość, wysokość, Hz).
        virtual DynamicArray<DisplayMode> GetSupportedDisplayModes() = 0;
        // Natywna rozdzielczość (tryb pulpitu) monitora, na którym stoi okno.
        virtual IVec2 GetDesktopResolution() = 0;

        virtual bool HasWindowFocus() = 0;

        virtual bool IsVSyncEnabled() = 0;
        virtual void SetVSyncEnabled(bool enabled) = 0;
        // Sets the swap interval on the GL context *without* touching the window's own vsync
        // setting. The context is shared by every window, so the render thread flips the interval
        // per swap (secondary windows present without vsync — see RenderingManager). Render thread
        // only; SetVSyncEnabled is what the rest of the engine should use.
        virtual void ApplySwapInterval(bool vsync) = 0;

        virtual void* GetWindowHandle() = 0;
        virtual void* GetGLContext() = 0;

        virtual void MakeGLContextCurrent() = 0;
        // Odpina kontekst GL od bieżącego wątku, aby inny wątek (render thread) mógł go przejąć.
        virtual void ReleaseGLContext() = 0;
        virtual void SwapBuffer() = 0;

        virtual void SetWindowTitle(String title) = 0;

        static Plu::TOwningPointer<IWindow> PlutexCreateWindow(const WindowProperties& properties, const TUsePointer<EngineObjectManager>& objectManager, ApplicationInfo *
                                                               applicationInfo);

        virtual void SetCursorVisibility(bool visible) = 0;
        virtual void SetCursorPosition(IVec2 pos) = 0;
        virtual IVec2 GetCursorPosition() = 0;

        // Kontekst ImGui tworzy i konfiguruje RenderingManager::InitializeImGuiContext()
        // (przez ImGuiRenderState); okno tylko go przechowuje dla WndProc/eventów.
        void SetImGuiContext(ImGuiContext* context) { mImGuiContext = context; }
        ImGuiContext* GetImGuiContext() const { return mImGuiContext; }

        bool ImGuiItemHovered = false;
        bool UpdateImGui = true;
    };
}

#endif //PLUENGINE_WINDOW_H