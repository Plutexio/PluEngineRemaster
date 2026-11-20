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
#include "PluBase/Log.h"
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
#include "PluEngine/Networking/Networking.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Gameplay/RenderSnapshotBuilder.h"
#include "PluEngine/Render/RenderThreading.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluBase/ThreadAffinity.h"
#include "PluEngine/Core/Threading/TripleBuffer.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/ThreadPool/ThreadPool.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"

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
extern void InitPluNetworkingReflection();
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

        constexpr float kIdleHeartbeatSeconds = 0.25f;
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

        if (!mApplicationInfo.AppWindow->IsRunning()) {
            PLU_CORE_CRITICAL("Main window failed to initialize! Aborting launch!");
            return;
        }

        mApplicationInfo.AppWindowsManager->RegisterWindow(
            mObjectManager->GetObjectAsOwner<IWindow>(*mApplicationInfo.AppWindow->GetEngineObjectHandle()));
        mApplicationInfo.AppWindow->GetObjectEventDispatcher()->Subscribe("WindowCloseRequested", [this](void*) {
            OnRequestedWindowClose(mApplicationInfo.AppWindow);
        });
        mApplicationInfo.AppRenderingManager->InitializeImGuiContext();
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

        TripleBuffer<RenderSnapshot*> renderTripleBuffer;
        RenderSnapshotBuilder renderSnapshotBuilder = RenderSnapshotBuilder(&renderTripleBuffer, &mApplicationInfo);

        mApplicationInfo.AppWindow->ReleaseGLContext();
        mApplicationInfo.AppRenderingManager->Initialize(&renderTripleBuffer);
        SetFrameDemandWakeCallback(&WakePlatformEventLoop);

        std::chrono::high_resolution_clock::time_point lastFrame = std::chrono::high_resolution_clock::now();

        const std::optional<double> profilerExportAfter = PresentArgument<double>(mArgumentParser, "--profiler-export-after");
        const std::optional<std::string> profilerExportPathArg = PresentArgument<std::string>(mArgumentParser, "--profiler-export-path");
        const String profilerExportPath = profilerExportPathArg ? String(profilerExportPathArg->c_str()) : String("profiler.csv");
        float profilerElapsed = 0.0f;
        bool profilerExported = false;

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
                const FrameDemand afterEvents = ConsumeFrameDemand();
                demand.RenderNow = demand.RenderNow || afterEvents.RenderNow;
                demand.MustPresent = demand.MustPresent || afterEvents.MustPresent;
            }
            const bool probeFrame = framesOnDemand && !demand.MustPresent;
            SetProbeFrame(probeFrame);
            if (framesOnDemand && !demand.RenderNow) {
                PLU_PROFILE_SCOPE("Main Idle Tick");
                mApplicationInfo.AppWindowsManager->ProcessPendingWindows();
                OnIdleTick(idleSeconds);
                if (mApplicationInfo.AppAssetManager) mApplicationInfo.AppAssetManager->ProcessPendingLoads();
                mApplicationInfo.AppWindowsManager->ProcessClosingWindows();
                exportProfilerIfDue(idleSeconds);
                continue;
            }
            PLU_PROFILE_SCOPE("Frame");
            mApplicationInfo.AppWindowsManager->ProcessPendingWindows();
            {
                PLU_PROFILE_SCOPE("Input Update");
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
            }
            {
                PLU_PROFILE_SCOPE("Input EndFrame");
                mApplicationInfo.AppInputManager->GetInputBackend()->EndFrame();
            }
            mApplicationInfo.AppWindowsManager->ProcessClosingWindows();
            {
                PLU_PROFILE_SCOPE("Main Frame Pacing");
                const float renderFPS = GetRenderThreadFPS();
                if (renderFPS > 0.0f) {
                    const float targetFrameTime = (1.0f / renderFPS) * 0.9f;
                    const float workElapsed = std::chrono::duration<float>(
                        std::chrono::high_resolution_clock::now() - frameStart).count();
                    float sleepFor = targetFrameTime - workElapsed;
                    sleepFor = ClampF(sleepFor, 0.0f, 0.05f);
                    if (sleepFor > 0.0f) {
                        std::this_thread::sleep_for(std::chrono::duration<float>(sleepFor));
                    }
                }
            }

            exportProfilerIfDue(deltaTime);
        }
        PLU_TIMER_START("EngineEnd");
        mApplicationInfo.AppRenderingManager->Shutdown();
        OnShutdown();
        mApplicationInfo.AppWindowsManager->Shutdown();
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

        REFLECTION_PRIMITIVE_TYPE(int);
        REFLECTION_PRIMITIVE_TYPE(Int8);
        REFLECTION_PRIMITIVE_TYPE(Int16);
        REFLECTION_PRIMITIVE_TYPE(Int32);
        REFLECTION_PRIMITIVE_TYPE(Int64);
        REFLECTION_PRIMITIVE_TYPE(UInt8);
        REFLECTION_PRIMITIVE_TYPE(UInt16);
        REFLECTION_PRIMITIVE_TYPE(UInt32);
        REFLECTION_PRIMITIVE_TYPE(UInt64);
        REFLECTION_PRIMITIVE_TYPE(float);
        REFLECTION_PRIMITIVE_TYPE(double);
        REFLECTION_PRIMITIVE_TYPE(bool);
        REFLECTION_PRIMITIVE_TYPE(Vec2);
        REFLECTION_PRIMITIVE_TYPE(Vec3);
        REFLECTION_PRIMITIVE_TYPE(Vec4);
        REFLECTION_PRIMITIVE_TYPE(IVec2);
        REFLECTION_PRIMITIVE_TYPE(IVec3);
        REFLECTION_PRIMITIVE_TYPE(IVec4);
        REFLECTION_PRIMITIVE_TYPE(Quaternion);
        REFLECTION_PRIMITIVE_TYPE(String);
        REFLECTION_PRIMITIVE_TYPE(StringW);
        REFLECTION_PRIMITIVE_TYPE(Path);
        REFLECTION_PRIMITIVE_TYPE(PathW);

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
        InitPluNetworkingReflection();
        InitPluAppReflection();

        InstallAssetReflectionHooks();
        InstallPythonObjectFactory();
        TypeRegistry::GetInstance()->serializeForTypeInfo = &TypeSerializer<TypeInfo*>::Serialize;
        TypeRegistry::GetInstance()->deserializeForTypeInfo = [](DeserializationContext* dc, const JSON& json, TypeInfo* typeInfo, void* instance) {
            TypeSerializer<TypeInfo*>::Deserialize(dc, json, typeInfo, instance);
        };
        Engine::CreateEngine();
        PLU_CORE_INFO("Engine Init");
        mObjectManager = Plu::CreateOwning<EngineObjectManager>();
        TypeRegistry::GetInstance()->mObjectManager = mObjectManager;
        mApplicationInfo.AppObjectManager = mObjectManager;

        mApplicationInfo.AppThreadPool = mObjectManager->CreateObject(ThreadPool::GetStaticClass());

        mApplicationInfo.AppRenderingManager = mObjectManager->GetObjectAsOwner<RenderingManager>(mObjectManager->CreateObject<RenderingManager>(&mApplicationInfo));

        mApplicationInfo.AppAssetManager = mObjectManager->CreateObject(EngineAssetManager::GetStaticClass());
        mApplicationInfo.AppAssetManager->Initialize(&mApplicationInfo);
        TypeRegistry::GetInstance()->mAssetManager = mApplicationInfo.AppAssetManager;

        mApplicationInfo.AppScenesManager = mObjectManager->CreateObject(SceneManager::GetStaticClass());

        mApplicationInfo.AppWindowsManager = mObjectManager->CreateObject(WindowsManager::GetStaticClass());
        mApplicationInfo.AppWindowsManager->Initialize(&mApplicationInfo);
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

        AnimationGraphVariableFactory::RegisterBuiltInTypes();
        ParticleParameterFactory::RegisterBuiltInTypes();

#ifdef PLU_PLATFORM_LINUX
        SDLWindow::InitSDL();
#endif

        JoltPhysics::Init(&mApplicationInfo);
        Networking::Init();
    }

    void Application::EngineShutdown()
    {
        Networking::Shutdown();
        JoltPhysics::Shutdown();
        mObjectManager->DestroyObject(*mApplicationInfo.AppRenderingManager->GetEngineObjectHandle());
        mApplicationInfo.AppRenderingManager = nullptr;

        mObjectManager->DestroyObject(*mApplicationInfo.AppWindow->GetEngineObjectHandle());
        mObjectManager->DestroyObject(*mApplicationInfo.AppWindowsManager->GetEngineObjectHandle());

        mObjectManager->DestroyObject(*mApplicationInfo.AppThreadPool->GetEngineObjectHandle());

        Engine::DestroyEngine();
        PLU_CORE_WARN("Engine Shutdown");
        mObjectManager = nullptr;
        PLU_TIMER_END("EngineEnd");
        TypeRegistry::GetInstance()->Cleanup();
    }

    void ExitGame()
    {
        gApplication->OnRequestedGameExit();
    }
}
