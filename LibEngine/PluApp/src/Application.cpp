//
// Created by Plutex on 12/30/25.
//

#include "PluEngine/Application.h"

#include <optional>
#include <thread>

#include "PluEngine/Engine.h"
#include "PluEngine/FrameDemand.h"
#include "PluBase/Log.h"
#include "PluEngine/Timer.h"
#include "PluEngine/AssetCore/AssetReflectionHooks.h"
#include "PluEngine/Scripting/PythonObjectFactory.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetTypes/AnimationGraph/AnimationGraph.h"
#include "PluEngine/AssetTypes/Image/ImageInfo.h"
#include "PluEngine/Effects/Particles/ParticleParameter.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Profiler.h"
#include "PluEngine/Platform/Platform.h"
#include "PluEngine/Platform/Window.h"
#include "PluEngine/Platform/WindowsManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Gameplay/GameClient.h"
#include "PluEngine/Gameplay/InputManager.h"
#include "PluEngine/Physics/JoltIntializer.h"
#include "PluEngine/Networking/Networking.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluBase/ThreadAffinity.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/ThreadPool/ThreadPool.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/Render/GraphicsAPIController.h"
#include "PluEngine/Vulkan/VulkanGraphicsController.h"

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
    }

    void Application::Run()
    {
        if (!OnInit()) {
            PLU_CORE_CRITICAL("Error during initialization! Aborting launch!");
            ShutdownEnter();
            return;
        }
        mApplicationInfo.AppWindow->Init();

        if (!mApplicationInfo.AppWindow->IsRunning()) {
            PLU_CORE_CRITICAL("Main window failed to initialize! Aborting launch!");
            ShutdownEnter();
            return;
        }

        mApplicationInfo.AppWindowsManager->RegisterWindow(
            mObjectManager->GetObjectAsOwner<IWindow>(*mApplicationInfo.AppWindow->GetEngineObjectHandle()));
        mApplicationInfo.AppWindow->GetObjectEventDispatcher()->Subscribe("WindowCloseRequested", [this](void*) {
            OnRequestedWindowClose(mApplicationInfo.AppWindow);
        });
        mApplicationInfo.AppInputBackend = mApplicationInfo.AppInputManager->GetInputBackend().GetRaw();
        mApplicationInfo.AppInputManager->GetInputBackend()->Init();
        //mApplicationInfo.AppPlatform->SpawnConsoleWindow();

        //I think its safe to Initialize Graphics here
        try {
            mApplicationInfo.AppGraphicsApiController = GraphicsApiController::CreateApiController(VulkanGraphicsController::GetStaticClass(), &mApplicationInfo);
        } catch (std::exception& e) {
            PLU_CORE_CRITICAL("Error during Graphics API Controller creation! {}", e.what());
            ShutdownEnter();
            return;
        }

        JoltPhysics::Init(&mApplicationInfo);

        mApplicationInfo.AppScenesManager->Initialize(&mApplicationInfo);
        OnPostInit();

        PLU_CORE_TRACE("Initialized Successfully!");
        PLU_TIMER_END("EngineInit");

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
            mApplicationInfo.AppPlatform->PollEvents();
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
        ShutdownEnter();
    }

    void Application::ShutdownEnter()
    {
        PLU_TIMER_START("EngineEnd");
        OnShutdown();
        mApplicationInfo.AppWindowsManager->Shutdown();
        mApplicationInfo.AppPlatform->Shutdown();
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
        InitializeImageRuntime();

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
        REFLECTION_PRIMITIVE_TYPE(UVec2);
        REFLECTION_PRIMITIVE_TYPE(UVec3);
        REFLECTION_PRIMITIVE_TYPE(UVec4);
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
        mObjectManager = Plu::CreateOwning<EngineObjectManager>();
        TypeRegistry::GetInstance()->mObjectManager = mObjectManager;
        mApplicationInfo.AppObjectManager = mObjectManager;

        mApplicationInfo.AppThreadPool = mObjectManager->CreateObject(ThreadPool::GetStaticClass());

        mApplicationInfo.AppAssetManager = mObjectManager->CreateObject(EngineAssetManager::GetStaticClass());
        mApplicationInfo.AppAssetManager->Initialize(&mApplicationInfo);
        TypeRegistry::GetInstance()->mAssetManager = mApplicationInfo.AppAssetManager;

        mApplicationInfo.AppScenesManager = mObjectManager->CreateObject(SceneManager::GetStaticClass());

        mApplicationInfo.AppPlatform = IPlatform::PlutexCreatePlatform(mObjectManager, &mApplicationInfo);
        mApplicationInfo.AppPlatform->Init();

        mApplicationInfo.AppWindowsManager = mObjectManager->CreateObject(WindowsManager::GetStaticClass());
        mApplicationInfo.AppWindowsManager->Initialize(&mApplicationInfo);

        AnimationGraphVariableFactory::RegisterBuiltInTypes();
        ParticleParameterFactory::RegisterBuiltInTypes();

        Networking::Init();
    }

    void Application::EngineShutdown()
    {
        Networking::Shutdown();
        JoltPhysics::Shutdown();

        mObjectManager->DestroyObject(*mApplicationInfo.AppWindow->GetEngineObjectHandle());
        mObjectManager->DestroyObject(*mApplicationInfo.AppWindowsManager->GetEngineObjectHandle());
        // After the windows: they unregister from the platform while shutting down.
        mObjectManager->DestroyObject(*mApplicationInfo.AppPlatform->GetEngineObjectHandle());

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
