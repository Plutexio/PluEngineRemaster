//
// Created by Plutex on 12/30/25.
//

#ifndef PLUENGINE_APPLICATION_H
#define PLUENGINE_APPLICATION_H
#include <PluSTL_FWD.h>
#include <argparse/argparse.hpp>

#include "PluEngine/Core.h"

#include "PluEngine/Core/ApplicationInfo.h"

namespace Plu
{
    class IWindow;

    class PLUAPP_API Application
    {
    protected:
        TOwningPointer<EngineObjectManager> mObjectManager;
        ApplicationInfo mApplicationInfo;

        // Null until InjectArguments() — an app whose main() does not call it (or calls it with
        // nullptr) must still be safe to read this.
        argparse::ArgumentParser* mArgumentParser = nullptr;
        friend class IWindow;
    public:
        Application();
        virtual ~Application();

        void InjectArguments(argparse::ArgumentParser* parser);

        // Registers the CLI arguments handled by the engine itself (profiler export). Call from
        // main() before parse_args(); app-specific arguments stay in the app's own main.
        static void AddEngineArguments(argparse::ArgumentParser& parser);

        void Run();
        void Close();

        virtual bool OnInit() = 0;
        virtual void OnPostInit() = 0;
        virtual void OnShutdown() = 0;
        // NOTE: the engine no longer drives an OnImGuiRender() callback. An app that wants a
        // UI builds its own ImGui frame (ImGui::NewFrame()/.../ImGui::Render()) wherever it
        // likes and hands the result to the render thread via
        // RenderingManager::SubmitImGuiDrawData(). Apps without UI (e.g. Runtime) skip it.
        virtual void OnTick(float deltaTime) = 0;
        // Power saving (FrameDemand.h). Run() calls this instead of OnTick() on the iterations where
        // nothing asked for a frame — roughly four times a second while idle. For cheap housekeeping
        // that must not stall with the UI (file watchers); call RequestRedraw() if it finds work.
        virtual void OnIdleTick(float deltaTime) {}
        // True while the app must produce every frame regardless of demand (a running game).
        virtual bool WantsContinuousFrames() { return false; }

        virtual void OnRequestedGameExit() = 0;
        virtual void OnRequestedWindowClose(TUsePointer<IWindow> window) = 0;

        TUsePointer<EngineObjectManager> GetAppObjectManager();
        TUsePointer<IWindow> GetAppWindow();
        ApplicationInfo* GetAppInfo();
    protected:
        void StartGame();
        void EndGame();

        void DispatchWindowClose(TUsePointer<IWindow> window);

        void EngineInit();
        void EngineShutdown();
    };

    PLU_FUNCTION()
    PLUAPP_API void ExitGame();
}

#endif //PLUENGINE_APPLICATION_H