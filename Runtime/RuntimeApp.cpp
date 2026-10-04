//
// Created by Plutex on 5/27/26.
//

#include "RuntimeApp.h"

#include "PluEngine/Gameplay/PluGame.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/Gameplay/GameClient.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Platform/Window.h"
#include "PluEngine/Gameplay/InputManager.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Core/CollisionChannels.h"
#include "PluEngine/Gameplay/Physics/PhysicsChannels.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "Python/RuntimePythonRunner.h"
#include "Shaders/RuntimeShaderManager.h"

Plu::RuntimeApp::RuntimeApp()
{
}

Plu::RuntimeApp::~RuntimeApp()
{
}

bool Plu::RuntimeApp::OnInit()
{
    PLU_INFO("Runtime Init");
    WindowProperties windowProperties;
    PathW selfPath = GetExePath();

    PLU_INFO("Path is {}", selfPath.ToString().ToNarrow().CStr());

    PathW projectPath = selfPath.GetParentPath();
    projectPath /= L"ProjectDefaults.json";
    if (!std::filesystem::exists(projectPath.CStr())) {
        return false;
    }
    // Written by EditorProjectManager::BuildProjectForShipment. Loaded before any world exists so
    // bodies are built against the project's channels.
    PathW physicsChannelsPath = selfPath.GetParentPath();
    physicsChannelsPath /= L"PhysicsChannels.bin";
    PhysicsChannelsManager::GetInstance()->LoadFromBinaryFile(physicsChannelsPath);
    StringW exeName = selfPath.GetStem();
    windowProperties.Title = exeName.ToNarrow();
    mApplicationInfo.AppWindow = IWindow::PlutexCreateWindow(windowProperties, mObjectManager, &mApplicationInfo);

    EngineObjectHandle inputManagerHandle = mObjectManager->CreateObject<InputManager>();
    mApplicationInfo.AppInputManager = mObjectManager->GetObjectAsUser<InputManager>(inputManagerHandle);

    EngineObjectHandle shaderManagerHandle = mObjectManager->CreateObject<RuntimeShaderManager>();
    TOwningPointer<RuntimeShaderManager> shaderManager = mObjectManager->GetObjectAsOwner<RuntimeShaderManager>(shaderManagerHandle);
    shaderManager->ShaderCodeScan();
    mApplicationInfo.AppShaderManager = shaderManager;
    shaderManager->InitAssetEvents(mApplicationInfo.AppAssetManager, mApplicationInfo.AppObjectManager);

    mApplicationInfo.AppAssetManager->ScanDirectory(selfPath.GetParentPath().ToString().ToNarrow());

    mApplicationInfo.AppPythonManager = mObjectManager->CreateObject(RuntimePythonRunner::GetStaticClass());
    DynamicCast<RuntimePythonRunner>(mApplicationInfo.AppPythonManager)->RunScripts(selfPath.GetParentPath().ToString().ToNarrow() + "/Scripts");
    return true;
}

void Plu::RuntimeApp::OnPostInit()
{
    PLU_INFO("Runtime Post Init");
    StartGame();
    mApplicationInfo.AppInputManager->GetInputBackend()->SetMouseCentered(true);
    PathW selfPath = GetExePath().GetParentPath();
    mGameStartupSettings = CreateOwning<GameStartupSettings>();
    selfPath += L"/ProjectDefaults.json";
    std::optional<JSON> json = DiskManager::LoadJson(selfPath);
    if (!json.has_value())
        return;
    mGameStartupSettings = CreateOwning<GameStartupSettings>();
    DeserializationContext* dc = mApplicationInfo.ConstructDeserializationContext();
    TypeSerializer<TypeInfo*>::Deserialize(dc, json, GameStartupSettings::GetStaticClass(), mGameStartupSettings.GetRaw());
    // UE-style collision channels: load the shipped project's config before scenes connect.
    if (json->contains("collisionConfig"))
        ActiveCollisionConfig() = LoadCollisionConfig((*json)["collisionConfig"]);
    TUsePointer<SceneInfo> sceneToLoadUUID = mGameStartupSettings->GameStartupScene;
    if (!sceneToLoadUUID)
    {
        OnRequestedWindowClose(mApplicationInfo.AppWindow);
        return;
    }
    mApplicationInfo.AppScenesManager->ConnectToWorld(sceneToLoadUUID->URL);
    if (mApplicationInfo.AppScenesManager->IsAnySceneOpen()) {
        mApplicationInfo.Client->JoinGameLocally();
    }
}

void Plu::RuntimeApp::OnShutdown()
{
    EndGame();
    mObjectManager->DestroyObject(*mApplicationInfo.AppScenesManager->GetEngineObjectHandle());
    mObjectManager->DestroyObject(*mApplicationInfo.AppAssetManager->GetEngineObjectHandle());
}

void Plu::RuntimeApp::OnTick(float deltaTime)
{
    static bool lastFocus = false;
    bool currentFocus = mApplicationInfo.AppWindow->HasWindowFocus();
    if (currentFocus != lastFocus)
    {
        if (currentFocus)
        {
            mApplicationInfo.AppWindow->SetCursorVisibility(false);
        } else
        {
            mApplicationInfo.AppWindow->SetCursorVisibility(true);
        }
        lastFocus = currentFocus;
    }
    //mApplicationInfo.AppRenderer->GetMainBuffer()->BlitToScreen(mApplicationInfo.AppWindow->GetWidth(), mApplicationInfo.AppWindow->GetHeight());
    //mApplicationInfo.AppWindow->SetCursorPosition(mApplicationInfo.AppWindow->GetCursorPosition() + IVec2(-mApplicationInfo.AppInputManager->GetInputBackend()->GetMouse().deltaX, -mApplicationInfo.AppInputManager->GetInputBackend()->GetMouse().deltaY));
}

void Plu::RuntimeApp::OnRequestedGameExit()
{
    OnRequestedWindowClose(mApplicationInfo.AppWindow);
}

void Plu::RuntimeApp::OnRequestedWindowClose(TUsePointer<IWindow> window)
{
    DispatchWindowClose(window);
}
