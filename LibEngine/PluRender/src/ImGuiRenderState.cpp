//
// Created by Plutex on 7/3/26.
//

#include "PluEngine/Render/ImGuiRenderState.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "PluEngine/Log.h"
#include "PluEngine/Platform/Window.h"

#ifdef PLU_PLATFORM_WINDOWS
#include "PluEngine/Platforms/Windows/WindowsWindow.h"
#include "imgui_impl_win32.h"
#elif defined(PLU_PLATFORM_LINUX)
#include "PluEngine/Platforms/Linux/SdlWindow.h"
#include "imgui_impl_sdl3.h"
#endif

// The "current ImGui context" global, made thread-local by our imgui overlay port
// (vcpkg-overlays/imgui/portfile.cmake defines GImGui to this symbol). Main and the render thread
// each have their own current context: Main builds one frame per window, the render thread submits
// the previous frame's draw data for those same windows. On Windows imgui is a DLL and keeps this
// storage itself (PLU_IMGUI_TLS_IN_DLL).
#ifndef PLU_IMGUI_TLS_IN_DLL
thread_local ImGuiContext* PluImGuiTLS = nullptr;
#endif

namespace Plu
{
    ImGuiContext* ImGuiRenderState::CreateContext(TUsePointer<IWindow> window, ImFontAtlas* sharedAtlas)
    {
        PLU_CORE_TRACE("Initializing ImGui Context...");
        IMGUI_CHECKVERSION();
        mContext = ImGui::CreateContext(sharedAtlas);
        ImGui::SetCurrentContext(mContext);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        // The renderer (OpenGL3) backend is initialized later on the RENDER thread (it needs
        // a current GL context). But ImGui::NewFrame() runs on the Main thread from frame 1,
        // and the font-atlas update path asserts that atlas->RendererHasTextures matches this
        // flag consistently. Set it here so the dynamic-texture mode is stable regardless of
        // when the render thread finishes ImGui_ImplOpenGL3_Init().
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

#ifdef PLU_PLATFORM_WINDOWS
        HWND windowHandle = static_cast<HWND>(window->GetWindowHandle());
        ImGui_ImplWin32_Init(windowHandle);
        // NOTE: ImGui_ImplOpenGL3_Init() is intentionally NOT called here - see render thread.
        PLU_CORE_WARN("Windows and OpenGL ImGui");
#elif defined(PLU_PLATFORM_LINUX)
        SDL_Window* windowHandle = static_cast<SDL_Window *>(window->GetWindowHandle());
        SDL_GLContext glContext = static_cast<SDL_GLContext>(window->GetGLContext());
        ImGui_ImplSDL3_InitForOpenGL(windowHandle, glContext);
        // NOTE: ImGui_ImplOpenGL3_Init() is intentionally NOT called here - it runs on the
        // render thread (RenderingManager::RenderThreadEnter) where the GL context lives.
        PLU_CORE_WARN("SDL3 and OpenGL ImGui");
#endif

        ApplyEngineStyle(window);
        return mContext;
    }

    void ImGuiRenderState::DestroyContext()
    {
        if (!mContext) return;
        ImGui::SetCurrentContext(mContext);
#ifdef PLU_PLATFORM_WINDOWS
        ImGui_ImplWin32_Shutdown();
#elif defined(PLU_PLATFORM_LINUX)
        ImGui_ImplSDL3_Shutdown();
#endif
        ImGui::DestroyContext(mContext);
        mContext = nullptr;
    }

    void ImGuiRenderState::InitRendererBackend(ImGuiContext* context)
    {
        // ImGui's GImGui is a single global; both threads only ever set it to this one context,
        // so it stays effectively constant - the render thread touches the renderer backend data,
        // Main touches the platform backend data.
        ImGui::SetCurrentContext(context);
        ImGui_ImplOpenGL3_Init("#version 450");
        mRendererBackendInitialized = true;
    }

    void ImGuiRenderState::ShutdownRendererBackend()
    {
        if (!mRendererBackendInitialized) return;
        // The backend's data hangs off this context's io.BackendRendererUserData, so it has to be
        // the current one — with several windows the render thread is rarely already on it.
        ImGui::SetCurrentContext(mContext);
        ImGui_ImplOpenGL3_Shutdown();
        mRendererBackendInitialized = false;
    }

    void ImGuiRenderState::ApplyEngineStyle(TUsePointer<IWindow> window)
    {
        ImGuiStyle& style = ImGui::GetStyle();
#ifdef PLU_PLATFORM_WINDOWS
        float mainScale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY));
        style.FontScaleDpi = mainScale;
        style.ScaleAllSizes(mainScale);
        //ImGui_ImplWin32_EnableDpiAwareness();
#elif defined(PLU_PLATFORM_LINUX)
        // HiDPI: scale ImGui to the display's content scale so the UI is physically sized like on
        // Windows. With ImGui 1.92's dynamic font atlas (RendererHasTextures set above) FontScaleDpi
        // re-rasterizes glyphs at the DPI density → crisp text. ConfigDpiScaleFonts keeps FontScaleDpi
        // in sync when the window moves to a monitor with a different scale.
        float mainScale = SDL_GetWindowDisplayScale(static_cast<SDL_Window*>(window->GetWindowHandle()));
        if (mainScale <= 0.0f) mainScale = 1.0f;
        ImGui::GetIO().ConfigDpiScaleFonts = true;
        style.FontScaleDpi = mainScale;
        style.ScaleAllSizes(mainScale);
#endif

        style.WindowRounding = 6.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 6.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;

        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;

        style.WindowPadding = ImVec2(12, 12);
        style.FramePadding = ImVec2(8, 6);

        ImVec4* colors = style.Colors;

        // Baza: czysto czarno-szara (bez niebieskiego odcienia), w pełni nieprzeźroczysta
        const ImVec4 bgDarkest   = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
        const ImVec4 bgDark      = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
        const ImVec4 bgMid       = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
        const ImVec4 bgLight     = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        const ImVec4 bgLighter   = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        const ImVec4 bgHover     = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
        const ImVec4 bgActive    = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
        const ImVec4 border      = ImVec4(0.03f, 0.03f, 0.03f, 1.00f);

        // Granatowy akcent zarezerwowany tylko dla drobnych elementów (checkmark, slider, cienka
        // linia aktywnej zakładki) - nigdy jako wypełnienie tła przycisków/tabów/nagłówków.
        const ImVec4 accent        = ImVec4(0.16f, 0.24f, 0.48f, 1.00f);
        const ImVec4 accentHovered = ImVec4(0.22f, 0.32f, 0.58f, 1.00f);
        const ImVec4 accentActive  = ImVec4(0.12f, 0.18f, 0.38f, 1.00f);
        const ImVec4 accentDim     = ImVec4(0.10f, 0.14f, 0.28f, 1.00f);

        colors[ImGuiCol_WindowBg]           = bgDark;
        colors[ImGuiCol_ChildBg]            = bgDark;
        colors[ImGuiCol_PopupBg]            = bgDark;
        colors[ImGuiCol_MenuBarBg]          = bgDarkest;
        colors[ImGuiCol_Border]             = border;
        colors[ImGuiCol_BorderShadow]       = ImVec4(0, 0, 0, 0);

        // Kolory kontrolne
        colors[ImGuiCol_FrameBg]            = bgMid;
        colors[ImGuiCol_FrameBgHovered]     = bgLight;
        colors[ImGuiCol_FrameBgActive]      = bgLighter;

        colors[ImGuiCol_Button]             = bgLight;
        colors[ImGuiCol_ButtonHovered]      = bgHover;
        colors[ImGuiCol_ButtonActive]       = bgActive;

        // Taby / DockSpace
        colors[ImGuiCol_Tab]                = bgMid;
        colors[ImGuiCol_TabHovered]         = bgHover;
        colors[ImGuiCol_TabActive]          = bgLighter;
        colors[ImGuiCol_TabDimmedSelected]  = bgLight;
        colors[ImGuiCol_TabDimmed]          = bgDarkest;
        colors[ImGuiCol_TabDimmedSelectedOverline] = accent;
        colors[ImGuiCol_TabSelectedOverline]       = accent;
        colors[ImGuiCol_DockingPreview]     = bgLighter;
        colors[ImGuiCol_DockingEmptyBg]     = bgDarkest;

        // Tekst
        colors[ImGuiCol_Text]               = ImVec4(0.92f, 0.92f, 0.93f, 1.00f);
        colors[ImGuiCol_TextDisabled]       = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]     = accentDim;

        colors[ImGuiCol_Header]             = bgLight;
        colors[ImGuiCol_HeaderHovered]      = bgHover;
        colors[ImGuiCol_HeaderActive]       = bgActive;

        colors[ImGuiCol_TitleBg]            = bgDarkest;
        colors[ImGuiCol_TitleBgActive]      = bgDarkest;
        colors[ImGuiCol_TitleBgCollapsed]   = bgDarkest;

        colors[ImGuiCol_CheckMark]          = accent;
        colors[ImGuiCol_SliderGrab]         = accent;
        colors[ImGuiCol_SliderGrabActive]   = accentHovered;

        colors[ImGuiCol_ScrollbarBg]        = bgDarkest;
        colors[ImGuiCol_ScrollbarGrab]      = bgLighter;
        colors[ImGuiCol_ScrollbarGrabHovered] = bgHover;
        colors[ImGuiCol_ScrollbarGrabActive]  = bgActive;

        colors[ImGuiCol_Separator]          = border;
        colors[ImGuiCol_SeparatorHovered]   = accentHovered;
        colors[ImGuiCol_SeparatorActive]    = accentActive;

        colors[ImGuiCol_ResizeGrip]         = bgLighter;
        colors[ImGuiCol_ResizeGripHovered]  = accentHovered;
        colors[ImGuiCol_ResizeGripActive]   = accentActive;

        colors[ImGuiCol_PlotLines]          = accent;
        colors[ImGuiCol_PlotLinesHovered]   = accentHovered;
        colors[ImGuiCol_PlotHistogram]      = accent;
        colors[ImGuiCol_PlotHistogramHovered] = accentHovered;

        colors[ImGuiCol_TableHeaderBg]      = bgMid;
        colors[ImGuiCol_TableBorderStrong]  = border;
        colors[ImGuiCol_TableBorderLight]   = border;
        colors[ImGuiCol_TableRowBg]         = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_TableRowBgAlt]      = ImVec4(1, 1, 1, 0.02f);

        colors[ImGuiCol_DragDropTarget]     = accent;
        colors[ImGuiCol_NavHighlight]       = accent;
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg]  = ImVec4(0, 0, 0, 0.50f);
        colors[ImGuiCol_ModalWindowDimBg]   = ImVec4(0, 0, 0, 0.50f);
    }
}
