//
// Created by Plutex on 1/5/26.
//

#include "PluEngine/Platforms/Linux/SdlWindow.h"

#include "glad/glad.h"
#include "imgui_impl_sdl3.h"
#include "PluEngine/Platform/SDLInputBackend.h"
#include "PluEngine/Core/Threading/ThreadAffinity.h"
#include "PluEngine/FrameDemand.h"

#ifdef PLU_PLATFORM_LINUX

namespace Plu
{
	#include "PluEngine/Platforms/Linux/SdlWindow.h"
    #include <GL/gl.h>

    void SDLWindow::InitSDL()
    {
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD);

        // OpenGL 3.3 Core
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    }

    HashMap<int, SDLWindow*> gSDLWindows;

    void SDLWindow::HandleSDLEvents()
    {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // Any OS event may change what is on screen. A bare pointer move usually does not, so
            // it only asks for probe frames (the UI decides whether anything changed). The wake
            // event is excluded: whoever pushed it has already stated what it needs.
            if (e.type == SDL_EVENT_MOUSE_MOTION) RequestProbe();
            else if (e.type != SDL_EVENT_USER) RequestRedraw();

            // Not every event carries a windowID (clipboard, quit, ...) — reading e.window.windowID
            // blindly is uninitialised stack memory there. SDL_GetWindowFromEvent knows which event
            // types have one and returns null for the rest (0 is never a valid SDL window ID).
            SDL_Window* eventWindow = SDL_GetWindowFromEvent(&e);
            Uint32 windowID = eventWindow ? SDL_GetWindowID(eventWindow) : 0;

            if (gSDLWindows.Contains(windowID)) {
                gSDLWindows[windowID]->OnEventSDL(&e);
            } else {
                if (e.type == SDL_EVENT_QUIT) {
                    for (auto window : gSDLWindows) {
                        window.second->DispatchEvent("WindowCloseRequested", nullptr);
                    }
                }
            }
        }
    }

    void SDLWindow::WaitForEvents(float timeoutSeconds)
    {
        // A null event leaves whatever arrived in the queue for HandleSDLEvents().
        SDL_WaitEventTimeout(nullptr, static_cast<Sint32>(timeoutSeconds * 1000.0f));
    }

    void SDLWindow::WakeEventLoop()
    {
        SDL_Event wake = {};
        wake.type = SDL_EVENT_USER;
        SDL_PushEvent(&wake);
    }

    void SDLWindow::SetCursorVisibility(bool visible)
    {
        if (visible) SDL_ShowCursor(); else SDL_HideCursor();
        // SDL3 dropped the global relative-mouse toggle; it's now per-window.
        SDL_SetWindowRelativeMouseMode(mWindow, !visible);
    }

    SDLWindow::SDLWindow()
    = default;

    SDLWindow::~SDLWindow()
    {
        Shutdown();
    }

#define MOUSE_GRAB_PADDING 10

    SDL_HitTestResult HitTestCallback(SDL_Window *Window, const SDL_Point *Area, void *Data)
    {
        int Width, Height;
        SDL_GetWindowSize(Window, &Width, &Height);

        SDLWindow** windowFind = gSDLWindows.Find(SDL_GetWindowID(Window));
        if (!windowFind) return SDL_HITTEST_DRAGGABLE;
        SDLWindow* window = *windowFind;

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

        return SDL_HITTEST_NORMAL; // SDL_HITTEST_NORMAL <- Windows behaviour
    }

    // The one GL context every engine window shares, created together with the first window.
    // Sharing it means textures/meshes/shaders uploaded once are visible in every window, and it
    // is the reason secondary windows must not create (or make current) a context of their own.
    static SDL_GLContext gSharedGLContext = nullptr;

    void SDLWindow::Init()
    {
        // SDL3 creates windows shown by default and dropped the x/y args from SDL_CreateWindow;
        // create hidden, position explicitly, then SDL_ShowWindow() below reveals it in place.
        // HIGH_PIXEL_DENSITY makes the GL drawable match the display's native pixels (not the
        // scaled-down logical size), so 3D + ImGui render crisp on HiDPI instead of being upscaled
        // by the compositor. Paired with GetWidth/GetHeight returning pixels (SDL_GetWindowSizeInPixels).
        SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
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
        if (mProperties.Position.x < 0 || mProperties.Position.y < 0) {
            SDL_SetWindowPosition(mWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        } else {
            SDL_SetWindowPosition(mWindow, mProperties.Position.x, mProperties.Position.y);
        }

        // Only the very first window sets up GL. A secondary window is created on the main thread
        // *while the render thread owns the context* — making it current here would rip the context
        // out from under that thread (instant crash), and GLAD is loaded already. Swap interval is a
        // property of the context, so it is left to the render thread as well (see RenderingManager).
        const bool isFirstWindow = gSharedGLContext == nullptr;
        if (isFirstWindow) {
            gSharedGLContext = SDL_GL_CreateContext(mWindow);
            mGLContext = gSharedGLContext;
            SDL_GL_MakeCurrent(mWindow, mGLContext);

            // GLAD ładujemy tu (main thread, świeżo bieżący kontekst), żeby wskaźniki funkcji GL
            // były gotowe zanim cokolwiek w inicjalizacji dotknie GL — analogicznie do
            // WindowsWindow::InitOpenGL. SDL3's SDL_GL_GetProcAddress returns SDL_FunctionPointer
            // (void(*)(void)); GLAD wants void*(*)(const char*) - the cast is the standard glue.
            if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
                PLU_CORE_CRITICAL("Failed to load GLAD!");
                std::terminate();
            }
            PLU_CORE_ASSERT(SDL_GL_GetCurrentContext() != nullptr, "GL Context is null!");

            SetVSyncEnabled(true);
        } else {
            mGLContext = gSharedGLContext;
            // A secondary window never waits on vsync: the interval belongs to the shared context
            // and only the main window's swap is allowed to block on it (see RenderingManager).
            // Recording that here keeps SwapBuffer's reconcile from flipping the interval back to 1
            // right after every secondary swap, i.e. from fighting the render loop each frame.
            mRequestedVSync = false;
            mVSyncEnabled = false;
        }

        SDL_SetWindowHitTest(mWindow, HitTestCallback, nullptr);

        mSDLWindowID = SDL_GetWindowID(mWindow);
        PLU_CORE_INFO("New Window created with engine ID {} (SDL ID {})", mWindowID, mSDLWindowID);
        mRunning = true;

        gSDLWindows.Insert(mSDLWindowID, this);

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

    void SDLWindow::Shutdown()
    {
        if (mWindow)
        {
            // Must come out of the dispatch map first: the event pump looks windows up by SDL id
            // and would keep dispatching into freed memory.
            gSDLWindows.Remove(mSDLWindowID);
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
        // Pixel size (not logical points): the render path uses this for the GL viewport, main
        // framebuffer and blit - they must match the HIGH_PIXEL_DENSITY drawable to stay crisp.
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
        if (IsOnMainThread()) {
            mRequestedVSync = enabled;
            return;
        }
        ApplySwapInterval(enabled);
        mVSyncEnabled = enabled;
    }

    void SDLWindow::ApplySwapInterval(bool vsync)
    {
        SDL_GL_SetSwapInterval(vsync ? 1 : 0);
        if (const char* msg = SDL_GetError()) {
            if (strlen(msg) != 0) {
                PLU_CORE_ERROR("SDL error: {}", msg);
            }
        }
    }

    void* SDLWindow::GetWindowHandle()
    {
        return mWindow;
    }

    void * SDLWindow::GetGLContext()
    {
        return mGLContext;
    }

    void SDLWindow::MakeGLContextCurrent()
    {
        SDL_GL_MakeCurrent(mWindow, mGLContext);
    }

    void SDLWindow::ReleaseGLContext()
    {
        SDL_GL_MakeCurrent(mWindow, nullptr);
    }

    void SDLWindow::SwapBuffer()
    {
        SDL_GL_SwapWindow(mWindow);
        if (mRequestedVSync != mVSyncEnabled && !IsOnMainThread()) {
            SetVSyncEnabled(mRequestedVSync);
        }
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
