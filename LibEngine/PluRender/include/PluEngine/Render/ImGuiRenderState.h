//
// Created by Plutex on 7/3/26.
//

#ifndef PLUENGINE_IMGUIRENDERSTATE_H
#define PLUENGINE_IMGUIRENDERSTATE_H

#include "PluSTL/PluSTL_FWD.h"
#include "PluEngine/Core.h"

struct ImGuiContext;
struct ImFontAtlas;

namespace Plu
{
    class IWindow;

    // Cykl życia kontekstu ImGui, rozdzielony między wątki tak jak backendy:
    // - CreateContext() na Main: kontekst + IO/DPI + styl silnika + backend platformowy
    //   (Win32/SDL3). Woła to RenderingManager::InitializeImGuiContext() zaraz po IWindow::Init().
    // - InitRendererBackend()/ShutdownRendererBackend() na render threadzie: backend OpenGL3
    //   (potrzebuje bieżącego kontekstu GL) — patrz RenderingManager::RenderThreadEnter/Exit.
    class PLURENDER_API ImGuiRenderState
    {
    public:
        // Main thread. Tworzy i konfiguruje kontekst ImGui dla okna; zwraca go, żeby okno
        // mogło go przechować (WndProc/eventy SDL czytają go przez IWindow::GetImGuiContext()).
        //
        // sharedAtlas: font atlas to reuse instead of creating a fresh one. Every window past the
        // first passes the first window's atlas — fonts are loaded once (PluEditor::OnPostInit) and
        // the atlas-rebuild lockstep stays a single, shared concern. Per-window DPI still works:
        // ImGui 1.92 bakes dynamic fonts per size, so style.FontScaleDpi may differ per context.
        ImGuiContext* CreateContext(TUsePointer<IWindow> window, ImFontAtlas* sharedAtlas = nullptr);

        // Main thread. Tears down the platform backend and the context itself. Only legal once the
        // render thread has released the renderer backend (see RenderingManager's teardown queues).
        void DestroyContext();

        // Render thread, z bieżącym kontekstem GL.
        void InitRendererBackend(ImGuiContext* context);
        void ShutdownRendererBackend();

        [[nodiscard]] ImGuiContext* GetContext() const { return mContext; }
        [[nodiscard]] bool IsRendererBackendInitialized() const { return mRendererBackendInitialized; }

    private:
        // Styl silnika: zaokrąglenia, paddingi i czarno-szara paleta z granatowym akcentem.
        void ApplyEngineStyle(TUsePointer<IWindow> window);

        ImGuiContext* mContext = nullptr;
        bool mRendererBackendInitialized = false;
    };
}

#endif //PLUENGINE_IMGUIRENDERSTATE_H
