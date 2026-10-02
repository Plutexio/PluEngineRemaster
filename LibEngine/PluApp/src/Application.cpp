//
// Created by Plutex on 12/30/25.
//

#include "PluEngine/Application.h"

#include <optional>
#include <thread>

#include "PluEngine/Platforms/Linux/SdlWindow.h"
#include "PluEngine/Platforms/Windows/WindowsWindow.h"
#include "PluEngine/Engine.h"
#include "PluEngine/FrameDemand.h"
#include "PluEngine/Log.h"
#include "PluEngine/Timer.h"
#include "PluEngine/AssetCore/AssetReflectionHooks.h"
#include "PluEngine/Scripting/PythonObjectFactory.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetTypes/AnimationGraph/AnimationGraph.h"
#include "PluEngine/Effects/Particles/ParticleParameter.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Render/RenderingManager.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Profiler.h"
#include "PluEngine/Platform/Window.h"
#include "PluEngine/Platform/WindowsManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Gameplay/GameClient.h"
#include "PluEngine/Gameplay/InputManager.h"
#include "PluEngine/Physics/JoltIntializer.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Gameplay/RenderSnapshotBuilder.h"
#include "PluEngine/Render/RenderThreading.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Core/Threading/ThreadAffinity.h"
#include "PluEngine/Core/Threading/TripleBuffer.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"

// One per engine module, declared in layer order: reflection registration now
// runs explicitly bottom-up instead of relying on link order inside one binary.
extern void InitPluCoreReflection();
extern void InitPluPlatformReflection();
extern void InitPluAssetCoreReflection();
extern void InitPluAssetTypesReflection();
extern void InitPluEffectsReflection();
extern void InitPluScriptingReflection();
extern void InitPluRenderReflection();
extern void InitPluAssetPipelineReflection();
extern void InitPluGameplayReflection();
extern void InitPluPhysicsReflection();
extern void InitPluAppReflection();

namespace Plu
{
    Application::Application()
    {
        PLU_TIMER_START("EngineInit");
        EngineInit();
    }

    Application::~Application()
    {
        EngineShutdown();
    }

    void Application::InjectArguments(argparse::ArgumentParser *parser)
    {
        mArgumentParser = parser;
    }

    void Application::AddEngineArguments(argparse::ArgumentParser& parser)
    {
        parser.add_argument("--profiler-export-after")
            .help("Write the profiler CSV this many seconds after the main loop starts, then keep running")
            .scan<'g', double>();
        parser.add_argument("--profiler-export-path")
            .help("Where --profiler-export-after writes its CSV (default: profiler.csv)");
    }

    namespace
    {
        // Reads an optional argument without caring whether the app's main registered it — argparse
        // throws std::logic_error for a name it has never seen, and not every executable adds the
        // engine arguments.
        template <typename T>
        std::optional<T> PresentArgument(argparse::ArgumentParser* parser, const char* name)
        {
            if (!parser) return std::nullopt;
            try {
                return parser->present<T>(name);
            } catch (...) {
                return std::nullopt;
            }
        }

        // Longest the idle main loop sleeps without looking around (power saving). Bounds how stale
        // OnIdleTick() housekeeping can get and how long a wake-up that got lost can go unnoticed.
        constexpr float kIdleHeartbeatSeconds = 0.25f;
        // Delta handed to the first frame after an idle wait. The real one is the length of the
        // nap, which would send anything integrating over time (camera, animation) flying.
        constexpr float kFrameDeltaAfterIdle = 1.0f / 60.0f;

        void WaitForPlatformEvents(float timeoutSeconds)
        {
#ifdef PLU_PLATFORM_LINUX
            SDLWindow::WaitForEvents(timeoutSeconds);
#elif defined(PLU_PLATFORM_WINDOWS)
            WindowsWindow::WaitForEvents(timeoutSeconds);
#endif
        }

        void WakePlatformEventLoop()
        {
#ifdef PLU_PLATFORM_LINUX
            SDLWindow::WakeEventLoop();
#elif defined(PLU_PLATFORM_WINDOWS)
            WindowsWindow::WakeEventLoop();
#endif
        }
    }

    void Application::Run()
    {
        if (!OnInit()) {
            PLU_CORE_CRITICAL("Error during initialization! Aborting launch!");
            return;
        }
        mApplicationInfo.AppWindow->Init();
        // The app creates and Init()s window 0 itself (it must exist before the GL context does);
        // the manager only takes it into its list so lookups by id and "any window focused" see it.
        mApplicationInfo.AppWindowsManager->RegisterWindow(
            mObjectManager->GetObjectAsOwner<IWindow>(*mApplicationInfo.AppWindow->GetEngineObjectHandle()));
        mApplicationInfo.AppWindow->GetObjectEventDispatcher()->Subscribe("WindowCloseRequested", [this](void*) {
            OnRequestedWindowClose(mApplicationInfo.AppWindow);
        });
        // Kontekst ImGui (+ styl, backend platformowy) tworzy RenderingManager — jeszcze na
        // main thread, przed inicjalizacją inputu i OnPostInit(), które mogą już dotykać ImGui.
        // Domyślny stan GL (depth test itd.) ustawia render thread w RenderThreadEnter() —
        // wcześniej robił to linuksowy SDLGLContext, przez co Windows startował bez depth testu.
        mApplicationInfo.AppRenderingManager->InitializeImGuiContext();
        // Publish the backend as soon as it exists — the app subclass created the input manager in
        // OnInit(), and windows start pumping OS events into the backend from the first frame of
        // the loop below. Setting it in StartGame() instead would leave it null for the whole
        // editor session, since StartGame only runs when a game does.
        mApplicationInfo.AppInputBackend = mApplicationInfo.AppInputManager->GetInputBackend().GetRaw();
        mApplicationInfo.AppInputManager->GetInputBackend()->Init();
#ifdef PLU_PLATFORM_WINDOWS
        //DynamicCast<WindowsWindow>(mApplicationInfo.AppWindow)->SpawnConsoleWindow();
        //PLU_CORE_TRACE("Console Window Spawned!");
#endif

        mApplicationInfo.AppScenesManager->Initialize(&mApplicationInfo);
        OnPostInit();

        PLU_CORE_TRACE("Initialized Successfully!");
        PLU_TIMER_END("EngineInit");

        //Just here we drop off the GL context from main thread to the render thread
        //I assume we have done all the preparations, and now we are ready to give control to render thread
        //It will from now on handle shader compilation, loading meshes and textures and of course rendering
        TripleBuffer<RenderSnapshot*> renderTripleBuffer;
        RenderSnapshotBuilder renderSnapshotBuilder = RenderSnapshotBuilder(&renderTripleBuffer, &mApplicationInfo);

        mApplicationInfo.AppWindow->ReleaseGLContext();
        mApplicationInfo.AppRenderingManager->Initialize(&renderTripleBuffer);
        // Requests for a frame coming from the render thread have to break the idle event wait.
        SetFrameDemandWakeCallback(&WakePlatformEventLoop);

        std::chrono::high_resolution_clock::time_point lastFrame = std::chrono::high_resolution_clock::now();

        // One-shot profiler dump (--profiler-export-after). Gives a scripted run the same CSV the
        // Profiler panel exports, which otherwise only comes out of a native save dialog. The app
        // keeps running afterwards — close the window or kill the process when done.
        const std::optional<double> profilerExportAfter = PresentArgument<double>(mArgumentParser, "--profiler-export-after");
        const std::optional<std::string> profilerExportPathArg = PresentArgument<std::string>(mArgumentParser, "--profiler-export-path");
        const String profilerExportPath = profilerExportPathArg ? String(profilerExportPathArg->c_str()) : String("profiler.csv");
        float profilerElapsed = 0.0f;
        bool profilerExported = false;

        // Placed after a frame's work so the dump includes that frame's samples.
        auto exportProfilerIfDue = [&](float elapsed) {
            if (!profilerExportAfter || profilerExported) return;
            profilerElapsed += elapsed;
            if (profilerElapsed < static_cast<float>(*profilerExportAfter)) return;
            profilerExported = true;
            if (DiskManager::SaveText(profilerExportPath.ToWide(), Profiler::GetInstance()->BuildCsv())) {
                PLU_CORE_INFO("Profiler exported to {} after {}s", profilerExportPath.CStr(), profilerElapsed);
            } else {
                PLU_CORE_ERROR("Profiler export to {} failed", profilerExportPath.CStr());
            }
        };

        while (mApplicationInfo.AppWindow && mApplicationInfo.AppWindow->IsRunning()) {
            // Power saving: with nothing asking for a frame, sleep on the OS event queue instead of
            // spinning. Whatever ends the wait (an event, a scheduled redraw, a wake from the render
            // thread, the heartbeat) is looked at below, after the events have been pumped.
            bool waitedForEvents = false;
            float idleSeconds = 0.0f;
            const bool framesOnDemand = IsPowerSavingEnabled() && !WantsContinuousFrames();
            FrameDemand demand;
            if (framesOnDemand) {
                demand = ConsumeFrameDemand();
                if (!demand.RenderNow) {
                    PLU_PROFILE_SCOPE("Main Idle Wait");
                    const std::chrono::high_resolution_clock::time_point waitStart = std::chrono::high_resolution_clock::now();
                    WaitForPlatformEvents(demand.WaitSeconds < kIdleHeartbeatSeconds ? demand.WaitSeconds : kIdleHeartbeatSeconds);
                    idleSeconds = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - waitStart).count();
                    waitedForEvents = true;
                }
            }

            const std::chrono::high_resolution_clock::time_point frameStart = std::chrono::high_resolution_clock::now();
            float deltaTime = waitedForEvents
                ? kFrameDeltaAfterIdle
                : std::chrono::duration<float>(frameStart - lastFrame).count();
            lastFrame = frameStart;

            if (deltaTime > 1.0f) {
#ifdef PLU_DEBUG
                PLU_CORE_CRITICAL("Unbelievably high deltaTime, capping to 1");
#endif
                deltaTime = 0.99f;
            }

            SetMainThreadDeltaTime(deltaTime);
#ifdef PLU_PLATFORM_LINUX
            SDLWindow::HandleSDLEvents();
#elif defined(PLU_PLATFORM_WINDOWS)
            mApplicationInfo.AppWindow->OnUpdate(deltaTime);
#endif
            if (framesOnDemand) {
                // Again, now that the events are in: a click pumped just above must not ride a
                // frame that was only going to be a probe.
                const FrameDemand afterEvents = ConsumeFrameDemand();
                demand.RenderNow = demand.RenderNow || afterEvents.RenderNow;
                demand.MustPresent = demand.MustPresent || afterEvents.MustPresent;
            }
            // Probe frame: tick and build the UI, but re-render the scene only if something beyond
            // a pointer move asked for it. The app decides for its own UI (IsProbeFrame()).
            const bool probeFrame = framesOnDemand && !demand.MustPresent;
            SetProbeFrame(probeFrame);
            if (framesOnDemand && !demand.RenderNow) {
                // Still nothing to draw: do the housekeeping that must not stall with the UI and go
                // back to sleep. No app tick, no scene update, no snapshot — the render thread sees
                // nothing new and stays asleep too.
                PLU_PROFILE_SCOPE("Main Idle Tick");
                mApplicationInfo.AppWindowsManager->ProcessPendingWindows();
                OnIdleTick(idleSeconds);
                if (mApplicationInfo.AppAssetManager) mApplicationInfo.AppAssetManager->ProcessPendingLoads();
                mApplicationInfo.AppWindowsManager->ProcessClosingWindows();
                exportProfilerIfDue(idleSeconds);
                continue;
            }
            PLU_PROFILE_SCOPE("Frame");
            // New windows get their platform handle before anything builds a frame for them, and
            // closed ones are torn down after the frame that stopped drawing them.
            mApplicationInfo.AppWindowsManager->ProcessPendingWindows();
            {
                PLU_PROFILE_SCOPE("Input Update");
                // Any engine window, not just the main one — otherwise input dies the moment a
                // secondary window takes focus.
                if (mApplicationInfo.AppWindowsManager->IsAnyWindowFocused()) mApplicationInfo.AppInputManager->GetInputBackend()->Update();
            }
            {
                PLU_PROFILE_SCOPE("App OnTick");
                OnTick(deltaTime);
            }
            {
                PLU_PROFILE_SCOPE("Scenes Update");
                if (mApplicationInfo.AppScenesManager) mApplicationInfo.AppScenesManager->OnUpdate(deltaTime);
            }
            {
                PLU_PROFILE_SCOPE("Process Pending Asset Loads");
                // Drain deferred load requests posted by the render thread (GetAssetDataNoLoad
                // misses) so their CPU data is in cache before the next snapshot is consumed.
                if (mApplicationInfo.AppAssetManager) mApplicationInfo.AppAssetManager->ProcessPendingLoads();
            }
            {
                PLU_PROFILE_SCOPE("Render Snapshot Building");
                if (probeFrame) {
                    renderSnapshotBuilder.DiscardFrame();
                } else {
                    renderSnapshotBuilder.BuildSnapshotAndPublish(deltaTime);
                    mApplicationInfo.AppRenderingManager->NotifyFrameAvailable();
                }
                //mApplicationInfo.AppRenderer->OnUpdate(deltaTime);
            }
            {
                PLU_PROFILE_SCOPE("Input EndFrame");
                mApplicationInfo.AppInputManager->GetInputBackend()->EndFrame();
            }
            mApplicationInfo.AppWindowsManager->ProcessClosingWindows();
            {
                // Frame pacing. The render thread is paced by VSync (SwapBuffer blocks on the refresh),
                // so with e.g. 60Hz it settles at ~16.6ms/frame while Main, having nothing to block on,
                // free-runs at thousands of fps and burns a core for nothing. Keep Main a touch ahead of
                // the render cadence: target a frame time slightly shorter than the render frame time so a
                // fresh snapshot is always ready before the render thread consumes it, then sleep off the
                // leftover budget. If Main's own work already overran that target there is no budget left —
                // skip the sleep and run at full speed (the intended behaviour when Main is the bottleneck).
                PLU_PROFILE_SCOPE("Main Frame Pacing");
                const float renderFPS = GetRenderThreadFPS();
                if (renderFPS > 0.0f) {
                    // 0.9 -> Main runs ~11% faster than render (e.g. ~66fps vs a 60Hz render). Proportional
                    // margin so it scales with the refresh rate (240Hz -> ~4.16ms render, ~3.75ms target).
                    const float targetFrameTime = (1.0f / renderFPS) * 0.9f;
                    const float workElapsed = std::chrono::duration<float>(
                        std::chrono::high_resolution_clock::now() - frameStart).count();
                    float sleepFor = targetFrameTime - workElapsed;
                    // Upper bound is a safety net, not a frame rate: the render delta is the time
                    // between presents, and a render thread that presents rarely must never be able
                    // to talk Main into napping for seconds.
                    sleepFor = ClampF(sleepFor, 0.0f, 0.05f);
                    if (sleepFor > 0.0f) {
                        std::this_thread::sleep_for(std::chrono::duration<float>(sleepFor));
                    }
                }
            }

            exportProfilerIfDue(deltaTime);
        }
        PLU_TIMER_START("EngineEnd");
        // Stop & join the render thread FIRST. OnShutdown() destroys scenes, cameras, viewports
        // and managers that the render thread reads every frame (textures/meshes/shaders via the
        // RenderingManager and the snapshot). If the render thread is still alive during teardown
        // it races those destructions and ends up calling SwapBuffer()/GetWidth() on an already
        // destroyed window (mWindow == nullptr) -> segfault.
        mApplicationInfo.AppRenderingManager->Shutdown();
        OnShutdown();
        // Secondary windows go last: the render thread is joined by now so nothing draws into them,
        // but OnShutdown() still legitimately reads them — the editor saves its window layout there
        // (position, size, contents), and a window destroyed beforehand simply drops out of it.
        mApplicationInfo.AppWindowsManager->Shutdown();
        // Render thread is joined and the loop is done — nobody reads or writes the snapshot
        // triple buffer anymore. Free the lazily allocated slots (BuildSnapshotAndPublish).
        for (RenderSnapshot*& slot : renderTripleBuffer.GetBuffersForTeardown()) {
            delete slot;
            slot = nullptr;
        }
#ifdef PLU_PLATFORM_LINUX
        SDL_Quit();
#endif
    }

    void Application::Close()
    {
    }

    TUsePointer<EngineObjectManager> Application::GetAppObjectManager()
    {
        return mObjectManager;
    }

    TUsePointer<IWindow> Application::GetAppWindow()
    {
        return mApplicationInfo.AppWindow;
    }

    ApplicationInfo * Application::GetAppInfo()
    {
        return &mApplicationInfo;
    }


    void Application::StartGame()
    {
        EngineObjectHandle gameClientHandle = mObjectManager->CreateObject<GameClient>(mObjectManager, mApplicationInfo.AppScenesManager, mApplicationInfo.AppInputManager, mApplicationInfo.AppWindow);
        mApplicationInfo.Client = mObjectManager->GetObjectAsUser<GameClient>(gameClientHandle);
        mApplicationInfo.AppInputManager->Init(mApplicationInfo.Client, mApplicationInfo.AppWindow);
        SetGameClient(mApplicationInfo.Client);
        PLU_CORE_INFO("Started Game!");
    }

    void Application::EndGame()
    {
        if (!mApplicationInfo.Client) return;
        // Before the client is destroyed: the input callbacks captured it.
        if (mApplicationInfo.AppInputManager) mApplicationInfo.AppInputManager->ClearInputSink();
        mObjectManager->DestroyObject(*mApplicationInfo.Client->GetEngineObjectHandle());
        mApplicationInfo.Client = nullptr;
    }

    void Application::DispatchWindowClose(TUsePointer<IWindow> window)
    {
        window->Close();
    }

    static Application* gApplication;

    void Application::EngineInit()
    {
        Plu::RegisterMainThread();
        gApplication = this;
        Plu::Log::Init();
        InitPluCoreReflection();
        InitPluPlatformReflection();
        InitPluAssetCoreReflection();
        InitPluAssetTypesReflection();
        InitPluEffectsReflection();
        InitPluScriptingReflection();
        InitPluRenderReflection();
        InitPluAssetPipelineReflection();
        InitPluGameplayReflection();
        InitPluPhysicsReflection();
        InitPluAppReflection();
        // Reflection is registered; now let the asset layer plug itself into it.
        InstallAssetReflectionHooks();
        InstallPythonObjectFactory();
        // TypeSerializer<T> falls back to these for a reflected struct/class with no specialization
        // of its own (e.g. a ParticleClass field inside a component). ReflectionBase.h cannot call
        // TypeSerializer<TypeInfo*> itself, so they are wired here — once, for the editor and the
        // runtime alike; without them such fields silently fail to save and load.
        TypeRegistry::GetInstance()->serializeForTypeInfo = &TypeSerializer<TypeInfo*>::Serialize;
        TypeRegistry::GetInstance()->deserializeForTypeInfo = [](DeserializationContext* dc, const JSON& json, TypeInfo* typeInfo, void* instance) {
            TypeSerializer<TypeInfo*>::Deserialize(dc, json, typeInfo, instance);
        };
        Engine::CreateEngine();
        PLU_CORE_INFO("Engine Init");
        mObjectManager = Plu::CreateOwning<EngineObjectManager>();
        TypeRegistry::GetInstance()->mObjectManager = mObjectManager;
        mApplicationInfo.AppRenderingManager = mObjectManager->GetObjectAsOwner<RenderingManager>(mObjectManager->CreateObject<RenderingManager>(&mApplicationInfo));
        mApplicationInfo.AppObjectManager = mObjectManager;

        mApplicationInfo.AppAssetManager = mObjectManager->CreateObject(EngineAssetManager::GetStaticClass());
        mApplicationInfo.AppAssetManager->Initialize(&mApplicationInfo);
        TypeRegistry::GetInstance()->mAssetManager = mApplicationInfo.AppAssetManager;

        mApplicationInfo.AppScenesManager = mObjectManager->CreateObject(SceneManager::GetStaticClass());

        mApplicationInfo.AppWindowsManager = mObjectManager->CreateObject(WindowsManager::GetStaticClass());
        mApplicationInfo.AppWindowsManager->Initialize(&mApplicationInfo);
        // The window manager drives per-window ImGui contexts but sits below the renderer that
        // owns them, so the renderer hands it the four operations it needs.
        {
            TUsePointer<RenderingManager> renderingManager = mApplicationInfo.AppRenderingManager;
            ImGuiWindowContextOps imGuiOps;
            imGuiOps.CreateForWindow = [renderingManager](TUsePointer<IWindow> window) {
                renderingManager->CreateImGuiContextForWindow(window);
            };
            imGuiOps.RequestTeardown = [renderingManager](UInt32 windowID) {
                renderingManager->RequestImGuiContextTeardown(windowID);
            };
            imGuiOps.IsTornDown = [renderingManager](UInt32 windowID) {
                return renderingManager->IsImGuiContextTornDown(windowID);
            };
            imGuiOps.DestroyForWindow = [renderingManager](UInt32 windowID) {
                renderingManager->DestroyImGuiContextForWindow(windowID);
            };
            mApplicationInfo.AppWindowsManager->SetImGuiContextOps(imGuiOps);
        }

        // Shared by Editor and Runtime so a Runtime build's factory isn't empty (previously only the
        // editor populated it — see AnimationGraphVariableFactory::RegisterBuiltInTypes).
        AnimationGraphVariableFactory::RegisterBuiltInTypes();
        ParticleParameterFactory::RegisterBuiltInTypes();

#ifdef PLU_PLATFORM_LINUX
        SDLWindow::InitSDL();
#endif

        JoltPhysics::Init(&mApplicationInfo);
    }

    void Application::EngineShutdown()
    {
        JoltPhysics::Shutdown();
        mObjectManager->DestroyObject(*mApplicationInfo.AppRenderingManager->GetEngineObjectHandle());
        mApplicationInfo.AppRenderingManager = nullptr;
        Engine::DestroyEngine();
        PLU_CORE_WARN("Engine Shutdown");
        mObjectManager = nullptr;
        //mWindow->Shutdown();
        PLU_TIMER_END("EngineEnd");
        // Last thing in the engine's life: free the whole reflection registry
        // (TypeInfo/PropertyInfo/EnumInfo). Nothing may touch reflection past this point.
        TypeRegistry::GetInstance()->Cleanup();
    }

    void ExitGame()
    {
        gApplication->OnRequestedGameExit();
    }
}
