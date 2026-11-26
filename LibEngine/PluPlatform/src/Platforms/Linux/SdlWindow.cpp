//
// Created by Plutex on 1/5/26.
//

#include "PluEngine/Platforms/Linux/SdlWindow.h"
#include "PluEngine/Platforms/Linux/SdlPlatform.h"

#include "imgui_impl_sdl3.h"
#include "PluEngine/Platform/SDLInputBackend.h"
#include "PluBase/ThreadAffinity.h"

#ifdef PLU_PLATFORM_LINUX

#include "AppIconPng.h"

namespace Plu
{

    void SDLWindow::SetCursorVisibility(bool visible)
    {
        if (visible) SDL_ShowCursor(); else SDL_HideCursor();
        SDL_SetWindowRelativeMouseMode(mWindow, !visible);
    }

    SDLWindow::SDLWindow()
    = default;

    SDLWindow::~SDLWindow()
    {
        Shutdown();
    }

#define MOUSE_GRAB_PADDING 10

    // Data is the SDLWindow that installed the callback (see SDLWindow::Init).
    SDL_HitTestResult HitTestCallback(SDL_Window *Window, const SDL_Point *Area, void *Data)
    {
        int Width, Height;
        SDL_GetWindowSize(Window, &Width, &Height);

        const SDLWindow* window = static_cast<const SDLWindow*>(Data);
        if (!window) return SDL_HITTEST_DRAGGABLE;

        if (window->ImGuiItemHovered) {
            return SDL_HITTEST_NORMAL;
        }

        if(Area->y < MOUSE_GRAB_PADDING)
        {
            if(Area->x < MOUSE_GRAB_PADDING)
            {
                return SDL_HITTEST_RESIZE_TOPLEFT;
            }
            else if(Area->x > Width - MOUSE_GRAB_PADDING)
            {
                return SDL_HITTEST_RESIZE_TOPRIGHT;
            }
            else
            {
                return SDL_HITTEST_RESIZE_TOP;
            }
        }
        else if(Area->y > Height - MOUSE_GRAB_PADDING)
        {
            if(Area->x < MOUSE_GRAB_PADDING)
            {
                return SDL_HITTEST_RESIZE_BOTTOMLEFT;
            }
            else if(Area->x > Width - MOUSE_GRAB_PADDING)
            {
                return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
            }
            else
            {
                return SDL_HITTEST_RESIZE_BOTTOM;
            }
        }
        else if(Area->x < MOUSE_GRAB_PADDING)
        {
            return SDL_HITTEST_RESIZE_LEFT;
        }
        else if(Area->x > Width - MOUSE_GRAB_PADDING)
        {
            return SDL_HITTEST_RESIZE_RIGHT;
        }
        else if(Area->y < 30)
        {
            return SDL_HITTEST_DRAGGABLE;
        }

        return SDL_HITTEST_NORMAL;
    }

    static void ApplyAppIcon(SDL_Window* window)
    {
        PLU_PROFILE_SCOPE("SDLWindow::ApplyAppIcon");

        SDL_IOStream* stream = SDL_IOFromConstMem(AppIcon::kPng, sizeof(AppIcon::kPng));
        SDL_Surface* icon = stream ? SDL_LoadPNG_IO(stream, true) : nullptr;
        if (!icon) {
            PLU_CORE_WARN("Failed to decode the embedded window icon: {}", SDL_GetError());
            return;
        }
        if (!SDL_SetWindowIcon(window, icon)) {
            PLU_CORE_WARN("Failed to set the window icon: {}", SDL_GetError());
        }
        SDL_DestroySurface(icon);
    }

    void SDLWindow::Init()
    {
        SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN;
        if (mProperties.Resizable) {
            flags |= SDL_WINDOW_RESIZABLE;
        }
        if (mProperties.Borderless) {
            flags |= SDL_WINDOW_BORDERLESS;
        }
        mWindow = SDL_CreateWindow(
            mProperties.Title.CStr(),
            mProperties.Width,
            mProperties.Height,
            flags
        );
        if (!mWindow) {
            PLU_CORE_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
            return;
        }
        if (mProperties.Position.x < 0 || mProperties.Position.y < 0) {
            SDL_SetWindowPosition(mWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        } else {
            SDL_SetWindowPosition(mWindow, mProperties.Position.x, mProperties.Position.y);
        }

        SDL_SetWindowHitTest(mWindow, HitTestCallback, this);

        mSDLWindowID = SDL_GetWindowID(mWindow);
        PLU_CORE_INFO("New Window created with engine ID {} (SDL ID {})", mWindowID, mSDLWindowID);
        mRunning = true;

        PLU_CORE_ASSERT(mApplicationInfo && mApplicationInfo->AppPlatform, "SDLWindow::Init needs the platform - create it before any window")
        mPlatform = StaticCast<SDLPlatform>(mApplicationInfo->AppPlatform);
        mPlatform->RegisterWindow(mSDLWindowID, this);

        ApplyAppIcon(mWindow);

        SDL_ShowWindow(mWindow);
    }

    void SDLWindow::OnUpdate(float deltaTime)
    {
    }

    void SDLWindow::OnEventSDL(SDL_Event *e)
    {
        // A window may legitimately have no ImGui context of its own (a secondary window before the
        // manager builds one for it, or a window that simply hosts no UI). Feeding its events to
        // whatever context happens to be current would land them in another window's ImGui, so the
        // whole ImGui path is skipped instead.
        const bool hasImGui = mImGuiContext != nullptr;
        if (hasImGui) ImGui::SetCurrentContext(mImGuiContext);

        if (e->type == SDL_EVENT_QUIT)
            mRunning = false;

        // Guarded like the WinAPI path: events can arrive before the app has built its input
        // manager (and a headless/tool build may never have one at all).
        if (mApplicationInfo && mApplicationInfo->AppInputBackend) {
            dynamic_cast<SDLInputBackend*>(mApplicationInfo->AppInputBackend)->FeedEvent(*e);
        }

        // Window lifecycle BEFORE ImGui gets a say. ImGui_ImplSDL3_ProcessEvent returns true for
        // CLOSE_REQUESTED / MOVED / RESIZED (it records them as PlatformRequest* on its viewport),
        // and the early return below would swallow them — the window manager's close button, and
        // "close all windows", would silently do nothing.
        // SDL3 replaced the SDL_WINDOWEVENT umbrella with distinct per-action event types.
        switch (e->type) {
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            {
                // Deferred: don't flip mRunning here. The app may want to confirm (unsaved
                // assets) before actually closing - see Application::Run()'s subscription and
                // Close() below, which is what actually flips mRunning.
                DispatchEvent("WindowCloseRequested", nullptr);
                break;
            }
            case SDL_EVENT_WINDOW_RESIZED:
            {
                mProperties.Width  = e->window.data1;
                mProperties.Height = e->window.data2;
                break;
            }
            default:
                break;
        }

        if (UpdateImGui && hasImGui) if (ImGui_ImplSDL3_ProcessEvent(e)) return;
        if (e->type == SDL_EVENT_DROP_FILE) {
            // In SDL3 drop.data is a const char* owned by SDL - copy it, do NOT SDL_free it.
            Path droppedPath(e->drop.data);
            DispatchEvent("FileDropped", &droppedPath);
            PLU_CORE_TRACE("Drop File, {}", droppedPath.CStr());
        }
        if (e->type == SDL_EVENT_DROP_BEGIN) {
            // drop.data is NULL on begin/complete. Fires when a drag carrying a payload enters the
            // window, before any actual drop - the only hook for "hovering with something to drop" UI.
            DispatchEvent("FileDragEntered", nullptr);
            PLU_CORE_TRACE("Drop Begin");
        }
        if (e->type == SDL_EVENT_DROP_COMPLETE) {
            DispatchEvent("FileDragEnded", nullptr);
            PLU_CORE_TRACE("Drop End");
        }

    }

    void SDLWindow::OnQuitRequested()
    {
        DispatchEvent("WindowCloseRequested", nullptr);
    }

    void SDLWindow::Shutdown()
    {
        if (mWindow)
        {
            // Must come out of the dispatch map first: the event pump looks windows up by SDL id
            // and would keep dispatching into freed memory.
            if (mPlatform) mPlatform->UnregisterWindow(mSDLWindowID);
            SDL_DestroyWindow(mWindow);
            mWindow = nullptr;
        }
    }

    bool SDLWindow::IsRunning()
    {
        return mRunning;
    }

    void SDLWindow::Close()
    {
        mRunning = false;
        DispatchEvent("WindowClosed", &mWindowID);
    }

    int SDLWindow::GetSDLWindowID() const
    {
        return mSDLWindowID;
    }

    IVec2 SDLWindow::GetWindowPosition()
    {
        int x = 0, y = 0;
        SDL_GetWindowPosition(mWindow, &x, &y);
        return {x, y};
    }

    void SDLWindow::SetWindowPosition(IVec2 position)
    {
        SDL_SetWindowPosition(mWindow, position.x, position.y);
    }

    int SDLWindow::GetWidth()
    {
        // Pixel size (not logical points): the render path sizes its viewport and render targets
        // from this - they must match the HIGH_PIXEL_DENSITY drawable to stay crisp.
        int w,h;
        SDL_GetWindowSizeInPixels(mWindow, &w, &h);
        return w;
    }

    int SDLWindow::GetHeight()
    {
        int w,h;
        SDL_GetWindowSizeInPixels(mWindow, &w, &h);
        return h;
    }

    void SDLWindow::Minimize()
    {
        SDL_MinimizeWindow(mWindow);
    }

    void SDLWindow::Maximize()
    {
        SDL_MaximizeWindow(mWindow);
    }

    bool SDLWindow::IsWindowMinimized()
    {
        return SDL_GetWindowFlags(mWindow) & SDL_WINDOW_MINIMIZED;
    }

    bool SDLWindow::IsWindowMaximized()
    {
        return SDL_GetWindowFlags(mWindow) & SDL_WINDOW_MAXIMIZED;
    }

    void SDLWindow::MakeFullscreen(FullscreenType type, IVec2 resolution)
    {
        if (type == FullscreenType::Windowed)
        {
            // SDL sam przywraca pozycję i rozmiar sprzed wejścia w fullscreen.
            SDL_SetWindowFullscreen(mWindow, false);
            SDL_SyncWindow(mWindow);
            mFullscreenType = type;
            return;
        }

        if (type == FullscreenType::BorderlessWindow)
        {
            // Tryb null = fullscreen desktop: okno kryje monitor, tryb wideo bez zmian.
            SDL_SetWindowFullscreenMode(mWindow, nullptr);
        }
        else
        {
            IVec2 target = resolution;
            if (target.x <= 0 || target.y <= 0) target = GetDesktopResolution();

            SDL_DisplayMode closest;
            const SDL_DisplayID display = SDL_GetDisplayForWindow(mWindow);
            // refresh_rate 0 = najwyższy dostępny dla tej rozdzielczości.
            if (!SDL_GetClosestFullscreenDisplayMode(display, target.x, target.y, 0.0f, true, &closest))
            {
                PLU_CORE_ERROR("No fullscreen mode matching {}x{}: {}", target.x, target.y, SDL_GetError());
                return;
            }
            SDL_SetWindowFullscreenMode(mWindow, &closest);
        }

        SDL_SetWindowFullscreen(mWindow, true);
        SDL_SyncWindow(mWindow);
        mFullscreenType = type;
    }

    FullscreenType SDLWindow::GetFullscreenType() const
    {
        return mFullscreenType;
    }

    DynamicArray<DisplayMode> SDLWindow::GetSupportedDisplayModes()
    {
        DynamicArray<DisplayMode> modes;

        int count = 0;
        SDL_DisplayMode** sdlModes = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(mWindow), &count);
        if (!sdlModes) return modes;

        modes.Reserve(count);
        for (int i = 0; i < count; ++i)
        {
            modes.PushBack({sdlModes[i]->w, sdlModes[i]->h, sdlModes[i]->refresh_rate});
        }
        SDL_free(sdlModes);

        // SDL zwraca malejąco; API silnika obiecuje rosnąco.
        modes.Sort([](const DisplayMode& a, const DisplayMode& b) {
            if (a.Width != b.Width) return a.Width < b.Width;
            if (a.Height != b.Height) return a.Height < b.Height;
            return a.RefreshRate < b.RefreshRate;
        });
        return modes;
    }

    IVec2 SDLWindow::GetDesktopResolution()
    {
        const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(mWindow));
        if (!desktop) return {0, 0};
        return {desktop->w, desktop->h};
    }

    bool SDLWindow::IsVSyncEnabled()
    {
        return mVSyncEnabled;
    }

    void SDLWindow::SetVSyncEnabled(bool enabled)
    {
        mVSyncEnabled = enabled;
    }

    void* SDLWindow::GetWindowHandle()
    {
        return mWindow;
    }

    void SDLWindow::SetWindowTitle(String title)
    {
        SDL_SetWindowTitle(mWindow, title.CStr());
    }

    void SDLWindow::SetCursorPosition(IVec2 pos)
    {
        SDL_WarpMouseInWindow(mWindow, pos.x, pos.y);
    }

    IVec2 SDLWindow::GetCursorPosition()
    {
        float x,y;
        SDL_GetMouseState(&x, &y);
        return {static_cast<int>(x), static_cast<int>(y)};
    }

    bool SDLWindow::HasWindowFocus()
    {
        SDL_WindowFlags flags = SDL_GetWindowFlags(mWindow);
        // We *don't* want to check mouse focus:
        // SDL_WINDOW_INPUT_FOCUS - input is going to the window
        // SDL_WINDOW_MOUSE_FOCUS - mouse is hovered over the window, regardless of window focus
        return (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    }
}

#endif
