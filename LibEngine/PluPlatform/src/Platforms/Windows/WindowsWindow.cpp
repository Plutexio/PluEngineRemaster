//
// Created by Plutex on 31.12.2025.
//

#include "PluEngine/Platforms/Windows/WindowsWindow.h"

#include "PluEngine/Log.h"
#include "PluEngine/FrameDemand.h"

#ifdef PLU_PLATFORM_WINDOWS


#include <atomic>
#include <dwmapi.h>
#include <windowsx.h>
#include <ole2.h>
#include <shellapi.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "glad/glad_wgl.h"
#include "PluEngine/Platform/WinAPIInputBackend.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandlerEx(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, ImGuiIO& io);

namespace Plu {

    HashMap<uintptr_t, WindowsWindow*> windows;

    // OLE drag & drop target - gives us drag-enter/leave hover notifications in addition to the
    // actual drop, mirroring SDL_EVENT_DROP_BEGIN/DROP_FILE/DROP_COMPLETE on the SDL platform.
    class WinDropTarget : public IDropTarget
    {
    public:
        explicit WinDropTarget(WindowsWindow* owner) : mOwner(owner) {}

        HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override
        {
            if (riid == IID_IUnknown || riid == IID_IDropTarget)
            {
                *ppvObject = static_cast<IDropTarget*>(this);
                AddRef();
                return S_OK;
            }
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        ULONG __stdcall AddRef() override
        {
            return ++mRefCount;
        }

        ULONG __stdcall Release() override
        {
            ULONG count = --mRefCount;
            if (count == 0) delete this;
            return count;
        }

        HRESULT __stdcall DragEnter(IDataObject* dataObject, DWORD keyState, POINTL pt, DWORD* effect) override
        {
            *effect = DROPEFFECT_COPY;
            mOwner->DispatchEvent("FileDragEntered", nullptr);
            return S_OK;
        }

        HRESULT __stdcall DragOver(DWORD keyState, POINTL pt, DWORD* effect) override
        {
            *effect = DROPEFFECT_COPY;
            return S_OK;
        }

        HRESULT __stdcall DragLeave() override
        {
            mOwner->DispatchEvent("FileDragEnded", nullptr);
            return S_OK;
        }

        HRESULT __stdcall Drop(IDataObject* dataObject, DWORD keyState, POINTL pt, DWORD* effect) override
        {
            *effect = DROPEFFECT_COPY;

            FORMATETC format = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            STGMEDIUM medium;
            if (SUCCEEDED(dataObject->GetData(&format, &medium)))
            {
                HDROP hDrop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
                if (hDrop)
                {
                    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
                    for (UINT i = 0; i < fileCount; ++i)
                    {
                        wchar_t wbuffer[MAX_PATH];
                        DragQueryFileW(hDrop, i, wbuffer, MAX_PATH);

                        char buffer[MAX_PATH * 4];
                        WideCharToMultiByte(CP_UTF8, 0, wbuffer, -1, buffer, sizeof(buffer), nullptr, nullptr);

                        Path droppedPath(buffer);
                        mOwner->DispatchEvent("FileDropped", &droppedPath);
                        PLU_CORE_TRACE("Drop File, {}", droppedPath.CStr());
                    }
                    GlobalUnlock(medium.hGlobal);
                }
                ReleaseStgMedium(&medium);
            }

            // No separate WM/OLE "drop complete" notification exists - ending the hover state here
            // mirrors SDL's DROP_COMPLETE, which fires right after the DROP_FILE batch.
            mOwner->DispatchEvent("FileDragEnded", nullptr);
            return S_OK;
        }

    private:
        WindowsWindow* mOwner;
        ULONG mRefCount = 1;
    };

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        WindowsWindow** windowFind = windows.Find((uintptr_t)hwnd);
        if (!windowFind)
        {
            PLU_CORE_ERROR("Invalid window handle");
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
        WindowsWindow* window = *windowFind;
        // Kontekst ImGui powstaje po Init() (RenderingManager::InitializeImGuiContext), a WndProc
        // dostaje komunikaty już w trakcie Init (SetWindowPos/ShowWindow) — stąd null check.
        if (window->UpdateImGui && window->GetImGuiContext())
        {
            if (LRESULT imgui = ImGui_ImplWin32_WndProcHandlerEx(hwnd, uMsg, wParam, lParam, window->GetImGuiContext()->IO))
            {
                return imgui;
            }
        }
        if (window->HasWindowFocus()) dynamic_cast<WinAPIInputBackend*>(window->mApplicationInfo->AppInputBackend)->FeedMessage(uMsg, wParam, lParam);
        switch (uMsg) {
        case WM_CLOSE:
            // Deferred: don't close here. The app may want to confirm (unsaved assets) before
            // actually closing - see Application::Run()'s subscription and Close() below, which
            // is what actually flips mIsRunning. Mirrors SDL_EVENT_WINDOW_CLOSE_REQUESTED.
            window->DispatchEvent("WindowCloseRequested", nullptr);
            return 0;
        case WM_SIZE:
            window->mProperties.Width = LOWORD(lParam);
            window->mProperties.Height = HIWORD(lParam);
            break;
        case WM_NCHITTEST:
            {
                if (window->ImGuiItemHovered)
                {
                    return HTCLIENT;
                }
                // W fullscreenie okno kryje monitor - strefy resize i przeciągania za titlebar
                // pozwoliłyby je z niego ściągnąć.
                if (window->mFullscreenType != FullscreenType::Windowed)
                {
                    return HTCLIENT;
                }
                POINT cursor = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd, &cursor);

                RECT rc;
                GetClientRect(hwnd, &rc);

                const int borderSize = 8; // strefa resize przy krawędziach

                // Resize zones
                if (cursor.y < borderSize) {
                    if (cursor.x < borderSize)       return HTTOPLEFT;
                    if (cursor.x > rc.right - borderSize) return HTTOPRIGHT;
                    return HTTOP;
                }
                if (cursor.y > rc.bottom - borderSize) {
                    if (cursor.x < borderSize)       return HTBOTTOMLEFT;
                    if (cursor.x > rc.right - borderSize) return HTBOTTOMRIGHT;
                    return HTBOTTOM;
                }
                if (cursor.x < borderSize)  return HTLEFT;
                if (cursor.x > rc.right - borderSize) return HTRIGHT;

                // Titlebar zone (np. 32px wysokości)
                const int titlebarHeight = 32;
                if (cursor.y < titlebarHeight)
                    return HTCAPTION; // Windows obsłuży przeciąganie i double-click maximize

                return HTCLIENT;
            }
        case WM_CREATE:
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            return 0;

        case WM_NCPAINT:
            return window->mProperties.Borderless ? 0 : DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_NCCALCSIZE:
            if (wParam == TRUE)
            {
                if (IsMaximized(hwnd))
                {
                    MONITORINFO mi = { sizeof(mi) };
                    GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
                    reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0] = mi.rcWork;
                }
                return window->mProperties.Borderless ? 0 : DefWindowProc(hwnd, uMsg, wParam, lParam);
            }
            break;

        case WM_ACTIVATE:
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            return 0;

        case WM_NCACTIVATE:
            return DefWindowProc(hwnd, uMsg, wParam, -1);

        case WM_GETMINMAXINFO:
            {
                // Bez tego WS_POPUP maksymalizuje się na cały ekran (pod pasek zadań)
                LPMINMAXINFO lpMMI = reinterpret_cast<LPMINMAXINFO>(lParam);
                HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { sizeof(mi) };
                GetMonitorInfo(hMonitor, &mi);

                // Pozycja i rozmiar zmaksymalizowanego okna = obszar roboczy monitora
                lpMMI->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                lpMMI->ptMaxPosition.y = mi.rcWork.top  - mi.rcMonitor.top;
                lpMMI->ptMaxSize.x     = mi.rcWork.right  - mi.rcWork.left;
                lpMMI->ptMaxSize.y     = mi.rcWork.bottom - mi.rcWork.top;
                return 0;
            }
        case WM_SETFOCUS:
            window->mHasFocus = true;
            break;
        case WM_KILLFOCUS:
            window->mHasFocus = false;
            break;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    Plu::WindowsWindow::WindowsWindow()
    {
        mIsRunning = false;
        mHandle = nullptr;
        mGLContext = nullptr;
        mHDC = nullptr;
        mhIcon = nullptr;
    }

    WindowsWindow::~WindowsWindow() {
    }

    void WindowsWindow::Close()
    {
        mIsRunning = false;
        DispatchEvent("WindowClosed", &mWindowID);
        PostQuitMessage(0);
    }

    void WindowsWindow::Maximize()
    {
        ShowWindow(static_cast<HWND>(mHandle), SW_MAXIMIZE);
    }

    void WindowsWindow::Minimize()
    {
        ShowWindow(static_cast<HWND>(mHandle), SW_MINIMIZE);
    }


    // ChangeDisplaySettingsEx/EnumDisplaySettings operują na monitorze (szDevice), nie na oknie.
    static bool GetMonitorDevice(HWND hwnd, MONITORINFOEXA& outInfo)
    {
        outInfo.cbSize = sizeof(MONITORINFOEXA);
        return GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST),
                               reinterpret_cast<LPMONITORINFO>(&outInfo));
    }

    void WindowsWindow::MakeFullscreen(FullscreenType type, IVec2 resolution)
    {
        MONITORINFOEXA monitor;
        if (!GetMonitorDevice(mHandle, monitor))
        {
            PLU_CORE_ERROR("GetMonitorInfo failed - cannot change fullscreen mode");
            return;
        }

        if (type == FullscreenType::Windowed)
        {
            // nullptr = powrót do trybu zapisanego w rejestrze (pulpitu).
            ChangeDisplaySettingsExA(monitor.szDevice, nullptr, nullptr, 0, nullptr);
            if (mWindowedStyle != 0)
            {
                SetWindowLong(mHandle, GWL_STYLE, mWindowedStyle);
                SetWindowPlacement(mHandle, &mWindowedPlacement);
                SetWindowPos(mHandle, nullptr, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            }
            mFullscreenType = type;
            return;
        }

        // Zapamiętujemy tylko przy wyjściu z Windowed - inaczej przełączenie
        // Fullscreen -> BorderlessWindow nadpisałoby stan okna geometrią monitora.
        if (mFullscreenType == FullscreenType::Windowed)
        {
            mWindowedStyle = GetWindowLong(mHandle, GWL_STYLE);
            mWindowedPlacement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(mHandle, &mWindowedPlacement);
        }

        if (type == FullscreenType::Fullscreen)
        {
            IVec2 target = resolution;
            if (target.x <= 0 || target.y <= 0) target = GetDesktopResolution();

            DEVMODEA mode = {};
            mode.dmSize = sizeof(DEVMODEA);
            mode.dmPelsWidth = target.x;
            mode.dmPelsHeight = target.y;
            mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

            const LONG result = ChangeDisplaySettingsExA(monitor.szDevice, &mode, nullptr, CDS_FULLSCREEN, nullptr);
            if (result != DISP_CHANGE_SUCCESSFUL)
            {
                PLU_CORE_ERROR("ChangeDisplaySettings to {}x{} failed ({})", target.x, target.y, result);
                return;
            }
            // Zmiana trybu przesuwa granice monitora - rcMonitor sprzed niej jest już nieaktualny.
            GetMonitorDevice(mHandle, monitor);
        }
        else
        {
            ChangeDisplaySettingsExA(monitor.szDevice, nullptr, nullptr, 0, nullptr);
        }

        const RECT& rc = monitor.rcMonitor;
        // WS_POPUP = brak ramki i paska tytułu; WS_VISIBLE, żeby SetWindowLong nie ukrył okna.
        SetWindowLong(mHandle, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(mHandle, HWND_TOP, rc.left, rc.top,
                     rc.right - rc.left, rc.bottom - rc.top, SWP_FRAMECHANGED);
        mFullscreenType = type;
    }

    FullscreenType WindowsWindow::GetFullscreenType() const
    {
        return mFullscreenType;
    }

    DynamicArray<DisplayMode> WindowsWindow::GetSupportedDisplayModes()
    {
        DynamicArray<DisplayMode> modes;

        MONITORINFOEXA monitor;
        if (!GetMonitorDevice(mHandle, monitor)) return modes;

        DEVMODEA mode = {};
        mode.dmSize = sizeof(DEVMODEA);
        for (DWORD i = 0; EnumDisplaySettingsA(monitor.szDevice, i, &mode); ++i)
        {
            // Tryby o mniejszej głębi to te same rozdzielczości w kółko - kontekst GL i tak ma 32 bpp.
            if (mode.dmBitsPerPel != 32) continue;
            modes.PushBack({static_cast<int>(mode.dmPelsWidth),
                            static_cast<int>(mode.dmPelsHeight),
                            static_cast<float>(mode.dmDisplayFrequency)});
        }

        modes.Sort([](const DisplayMode& a, const DisplayMode& b) {
            if (a.Width != b.Width) return a.Width < b.Width;
            if (a.Height != b.Height) return a.Height < b.Height;
            return a.RefreshRate < b.RefreshRate;
        });

        // EnumDisplaySettings powtarza ten sam tryb dla wariantów, których nie rozróżniamy
        // (interlaced, orientacja) - po sortowaniu duplikaty stoją obok siebie.
        DynamicArray<DisplayMode> unique;
        unique.Reserve(modes.Size());
        for (const DisplayMode& m : modes)
        {
            if (!unique.IsEmpty())
            {
                const DisplayMode& last = unique[unique.Size() - 1];
                if (last.Width == m.Width && last.Height == m.Height && last.RefreshRate == m.RefreshRate) continue;
            }
            unique.PushBack(m);
        }
        return unique;
    }

    IVec2 WindowsWindow::GetDesktopResolution()
    {
        MONITORINFOEXA monitor;
        if (!GetMonitorDevice(mHandle, monitor)) return {0, 0};

        // REGISTRY, nie CURRENT: po wejściu w wyłączny fullscreen (CDS_FULLSCREEN nie zapisuje się
        // do rejestru) CURRENT zwróciłby rozdzielczość fullscreena zamiast tej pulpitu.
        DEVMODEA mode = {};
        mode.dmSize = sizeof(DEVMODEA);
        if (!EnumDisplaySettingsA(monitor.szDevice, ENUM_REGISTRY_SETTINGS, &mode)) return {0, 0};
        return {static_cast<int>(mode.dmPelsWidth), static_cast<int>(mode.dmPelsHeight)};
    }

    void* WindowsWindow::GetWindowHandle()
    {
        return mHandle;
    }

    bool WindowsWindow::HasWindowFocus()
    {
        return mHasFocus;
    }

    bool WindowsWindow::IsVSyncEnabled()
    {
        return mVSyncEnabled;
    }

    void WindowsWindow::SetVSyncEnabled(bool enable)
    {
        mVSyncEnabled = enable;
        ApplySwapInterval(enable);
    }

    void WindowsWindow::ApplySwapInterval(bool vsync)
    {
        typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int);
        static PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT =
            (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");

        if (wglSwapIntervalEXT)
            wglSwapIntervalEXT(vsync ? 1 : 0);
    }

    void WindowsWindow::SetWindowTitle(String title)
    {
        SetWindowTextA(mHandle, title.CStr());
    }


    void* WindowsWindow::GetGLContext()
    {
        return static_cast<void*>(mGLContext);
    }

    void WindowsWindow::SpawnConsoleWindow()
    {
        if (!AllocConsole())
            return; // konsola już istnieje lub błąd

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
      
        auto& logger = Log::GetCoreLogger();
        if (logger)
            logger->sinks().push_back(stdout_sink);

        logger = Log::GetClientLogger();

        if (logger)
            logger->sinks().push_back(stdout_sink);

        PLU_CORE_INFO("Console Allocated");
    }

    void WindowsWindow::MakeGLContextCurrent()
    {
        wglMakeCurrent(mHDC, mGLContext);
    }

    void WindowsWindow::ReleaseGLContext()
    {
        wglMakeCurrent(nullptr, nullptr);
    }

    void WindowsWindow::SwapBuffer()
    {
        SwapBuffers(mHDC);
    }

    void WindowsWindow::SetCursorVisibility(bool visible)
    {
        // ShowCursor keeps an internal counter, not a boolean state - the cursor is shown
        // only while the count is >= 0. Pump it until it reflects the state we want so
        // repeated/unbalanced calls (entering/exiting PIE, F8) stay idempotent.
        if (visible)
        {
            while (ShowCursor(TRUE) < 0) {}
            ClipCursor(nullptr);
        } else
        {
            while (ShowCursor(FALSE) >= 0) {}
            RECT rect;
            GetWindowRect(mHandle, &rect);
            ClipCursor(&rect);
        }
    }

    IVec2 WindowsWindow::GetCursorPosition()
    {
        POINT p;
        GetCursorPos(&p);
        return IVec2(p.x, p.y);
    }

    void WindowsWindow::SetCursorPosition(IVec2 pos)
    {
        SetCursorPos(pos.x, pos.y);
        // Keep the input backend's baseline in sync with this warp, otherwise its frame-to-frame
        // absolute-position delta counts the warp as real mouse movement next frame - the
        // drag-look "snap back". SDL avoids this via relative-motion state; WinAPI diffs positions.
        if (mApplicationInfo && mApplicationInfo->AppInputBackend)
        {
            if (PlatformInputBackend* backend = mApplicationInfo->AppInputBackend)
                backend->ResyncMousePosition();
        }
    }

    bool WindowsWindow::IsRunning()
    {
        return mIsRunning;
    }

    int WindowsWindow::GetWidth()
    {
        RECT rect;
        GetClientRect(static_cast<HWND>(mHandle), &rect);
        return rect.right - rect.left;
    }

    int WindowsWindow::GetHeight()
    {
        RECT rect;
        GetClientRect(static_cast<HWND>(mHandle), &rect);
        return rect.bottom - rect.top;
    }

    IVec2 WindowsWindow::GetWindowPosition()
    {
        RECT rect;
        GetWindowRect(mHandle, &rect);
        return {rect.left, rect.top};
    }

    void WindowsWindow::SetWindowPosition(IVec2 position)
    {
        SetWindowPos(mHandle, nullptr, position.x, position.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }


    bool WindowsWindow::IsWindowMaximized()
    {
        return IsZoomed(static_cast<HWND>(mHandle));
    }

    bool WindowsWindow::IsWindowMinimized()
    {
        return IsIconic(static_cast<HWND>(mHandle));
    }


    void WindowsWindow::Init() {

        WNDCLASS wc = {};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = "EngineWindowClass";

        if (!GetClassInfo(GetModuleHandle(nullptr), wc.lpszClassName, &wc))
        {
            PLU_CORE_INFO("Registering Window Class!");
            if (!RegisterClass(&wc))
            {
                PLU_CORE_ASSERT(false, "RegisterClass failed")
                return;
            }
        } else
        {
            PLU_CORE_INFO("Window class already registered!");
        }

        DWORD dwStyle = WS_OVERLAPPEDWINDOW;

        mHandle = CreateWindowEx(
            WS_EX_APPWINDOW,
            wc.lpszClassName,
            mProperties.Title.CStr(),
            dwStyle,
            CW_USEDEFAULT, CW_USEDEFAULT,
            mProperties.Width, mProperties.Height,
            nullptr, nullptr, GetModuleHandle(nullptr), nullptr
        );

        PLU_CORE_ASSERT(mHandle, "Invalid Handle")

        mHDC = GetDC(mHandle);
        if (!InitOpenGL(mHandle))
        {
            PLU_CORE_CRITICAL("Invalid OpenGL Context!");
            return;
        }
        wglMakeCurrent(mHDC, mGLContext);
        mIsRunning = true;
        ShowWindow(mHandle, SW_SHOW);
        uintptr_t id = (uintptr_t)mHandle;
        PLU_CORE_INFO("New Window ID: {}", id);
        windows[id] = this;
        PLU_CORE_WARN("Windows Window Initialized");
        SetWindowPos(mHandle, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        mHasFocus = GetForegroundWindow() == mHandle;

        OleInitialize(nullptr);
        mDropTarget = new WinDropTarget(this);
        RegisterDragDrop(mHandle, mDropTarget);
    }

    void WindowsWindow::OnUpdate(float deltaTime)
    {
        MSG msg = {};
        // hWnd = nullptr (not mHandle): cross-process OLE drag&drop delivers IDropTarget calls as
        // RPC messages routed through a hidden window OleInitialize creates on this thread, not
        // through mHandle. Filtering PeekMessage to mHandle starves that hidden window's queue, so
        // DragEnter/DragOver/Drop arrive late or never - this is what caused the drop overlay to
        // flicker and file drops to silently do nothing.
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

    // Thread that pumps the messages, i.e. the one WaitForEvents() blocks. Captured there rather
    // than at static init so it does not depend on which thread loaded the module.
    static std::atomic<DWORD> gMessageThreadId{0};

    void WindowsWindow::WaitForEvents(float timeoutSeconds)
    {
        gMessageThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
        // MWMO_INPUTAVAILABLE: also return for input that was already queued before the call, not
        // only for input arriving during it.
        MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(timeoutSeconds * 1000.0f), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }

    void WindowsWindow::WakeEventLoop()
    {
        const DWORD threadId = gMessageThreadId.load(std::memory_order_relaxed);
        if (threadId != 0) PostThreadMessage(threadId, WM_NULL, 0, 0);
    }

    void WindowsWindow::Shutdown() {
        if (mDropTarget)
        {
            RevokeDragDrop(mHandle);
            static_cast<WinDropTarget*>(mDropTarget)->Release();
            mDropTarget = nullptr;
        }
        OleUninitialize();

        DestroyOpenGL();
        DestroyWindow(mHandle);
    }

    void WindowsWindow::DestroyOpenGL()
    {
        if (mGLContext)
        {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(mGLContext);
            mGLContext = nullptr;
        }
    }

    HGLRC WindowsWindow::InitOpenGL(HWND hWnd)
    {
        HDC hDC = GetDC(hWnd);

        // --- KROK 1: Tymczasowy kontekst (Legacy) ---
        if (!SetupPixelFormat(hDC)) return nullptr;

        HGLRC tempContext = wglCreateContext(hDC);
        wglMakeCurrent(hDC, tempContext);

        // --- KROK 2: Inicjalizacja GLAD i WGL ---
        if (!gladLoadGL() || !gladLoadWGL(hDC)) {
            // Błąd inicjalizacji
            wglMakeCurrent(NULL, NULL);
            wglDeleteContext(tempContext);
            return nullptr;
        }

        // --- KROK 3: Tworzenie nowoczesnego kontekstu 4.6 Core ---
        // Atrybuty dla OpenGL 4.6 Core Profile
        int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
            WGL_CONTEXT_MINOR_VERSION_ARB, 6,
            WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
    #ifdef _DEBUG
            WGL_CONTEXT_FLAGS_ARB,         WGL_CONTEXT_DEBUG_BIT_ARB,
    #endif
            0
        };

        HGLRC modernContext = nullptr;
        if (wglCreateContextAttribsARB) {
            modernContext = wglCreateContextAttribsARB(hDC, 0, attribs);
        }

        // --- KROK 4: Sprzątanie ---
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(tempContext); // Usuwamy stary, mamy nowy!

        if (modernContext) {
            wglMakeCurrent(hDC, modernContext);
        }

        mGLContext = modernContext;
        return modernContext;
    }

    bool WindowsWindow::SetupPixelFormat(HDC hdc)
    {
        PIXELFORMATDESCRIPTOR pfd = {
            sizeof(PIXELFORMATDESCRIPTOR),
            1,
            PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
            PFD_TYPE_RGBA,
            32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            24, // 24-bitowy bufor głębi (depth buffer)
            8,  // 8-bitowy bufor szablonu (stencil buffer)
            0, PFD_MAIN_PLANE, 0, 0, 0, 0
        };

        int pixelFormat = ChoosePixelFormat(hdc, &pfd);
        if (pixelFormat == 0) return false;

        return SetPixelFormat(hdc, pixelFormat, &pfd);
    }
} // namespace Engine

#endif
