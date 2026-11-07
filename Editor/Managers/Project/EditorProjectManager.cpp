//
// Created by Plutex on 1/6/26.
//

#include "EditorProjectManager.h"

#include <utility>
#include "EditorAppContext.h"
#include "EditorWindows/EditorWindowsManager.h"
#include "json_fwd.hpp"
#include "PythonBuildEnvironment.h"
#include "DefinedPanels/ProjectLauncherPanel.h"
#include "DefinedPanels/Project/AssetBrowserPanel/AssetBrowserPanel.h"
#include "DefinedViewports/Scene/SceneViewport.h"
#include "EditorViewports/EditorViewportManager.h"
#include "Managers/Assets/EditorAssetManager.h"
#include "Managers/Python/EditorPythonManager.h"
#include "Managers/Shaders/EditorShaderManager.h"
#include "Panels/EditorPanelManager.h"
#include "PluEngine/Application.h"
#include "PluEngine/Gameplay/PluGame.h"
#include "PluEngine/PluPaths.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Core/CollisionChannels.h"
#include "PluEngine/Gameplay/Physics/PhysicsChannels.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Platform/Window.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"

namespace Plu
{
	EditorProjectManager::EditorProjectManager()
	{
		mCurrentProjectPath = L"";
	}

	EditorProjectManager::~EditorProjectManager()
	{
	}

	void EditorProjectManager::Shutdown()
	{
		if (!IsAnyProjectOpen()) return;
		JSON j = DiskManager::LoadJson(GetProjectPath());
		JSON jO = TypeSerializer<TypeInfo*>::Serialize(GameStartupSettings::GetStaticClass(), mGameStartupSettings.GetRaw());
		jO["projectFileVersion"] = j["projectFileVersion"];
		jO["collisionConfig"] = SaveCollisionConfig(ActiveCollisionConfig());
		DiskManager::SaveJson(GetProjectPath().ToString(), jO);
		SavePhysicsChannels();
	}

	void EditorProjectManager::SetEditorAppContext(EditorAppContext *appContext, ApplicationInfo* applicationInfo)
	{
		mEditorAppContext = appContext;
		mApplicationInfo = applicationInfo;
	}

	EditorAppContext * EditorProjectManager::GetAppContext()
	{
		return mEditorAppContext;
	}

	bool EditorProjectManager::IsAnyProjectOpen() const
	{
		if (mCurrentProjectPath != L"" && mCurrentProjectPath.GetExtension() == PLU_PROJECT_EXT_W) return true;
		return false;
	}

	PathW EditorProjectManager::GetProjectDirectory() const
	{
		if (!IsAnyProjectOpen()) return L"";
		return mCurrentProjectPath.GetParentPath().ToString() + L"/";
	}

	StringW EditorProjectManager::GetProjectName() const
	{
		if (!IsAnyProjectOpen()) return L"";
		return mCurrentProjectPath.GetStem();
	}

	PathW EditorProjectManager::GetProjectPath() const
	{
		return mCurrentProjectPath.ToString();
	}

	void EditorProjectManager::BuildProjectForShipment(PathW dir)
	{
		PythonEnvironment pythonEnvironment;
		if (!pythonEnvironment.Setup()) {
			PLU_ERROR("No Python found!");
			return;
		}
		pythonEnvironment.Obfuscate(GetProjectScriptsDirectory().CStr(), (GetProjectCacheDirectory().ToString() + L"/ProjectDist").CStr());
		if (!std::filesystem::exists((GetProjectCacheDirectory().ToString() + L"/ProjectDist/Scripts/pyarmor_runtime_000000").CStr())) {
			std::filesystem::rename((GetProjectCacheDirectory().ToString() + L"/ProjectDist/pyarmor_runtime_000000").CStr(), (GetProjectCacheDirectory().ToString() + L"/ProjectDist/Scripts/pyarmor_runtime_000000").CStr());
		}
		TUsePointer<GameStartupSettings> gameStartupSettings = mGameStartupSettings;
		nlohmann::json json = TypeSerializer<TypeInfo*>::Serialize(gameStartupSettings->GetClass(), gameStartupSettings.GetRaw());
		json["collisionConfig"] = SaveCollisionConfig(ActiveCollisionConfig());
		DiskManager::SaveJson((GetProjectCacheDirectory().ToString() + L"/ProjectDist/ProjectDefaults.json").CStr(), json);
		PhysicsChannelsManager::GetInstance()->SaveToBinaryFile(GetProjectCacheDirectory().ToString() + L"/ProjectDist/PhysicsChannels.bin");
	}

	float EditorProjectManager::GetProjectFileVersion() const
	{
		return mProjectFileVersion;
	}

	TUsePointer<GameStartupSettings> EditorProjectManager::GetGameStartupSettings() const
	{
		return mGameStartupSettings;
	}

	PathW EditorProjectManager::GetRecentProjectsJSONPath()
	{
		const PathW exeDir = GetSystemUserPath().ToString().ToWide();
		PathW recentProjectJsonPath = exeDir / L".local/share/PluEngine/PluEditor/RecentProjects.json";
		std::filesystem::create_directories(recentProjectJsonPath.GetParentPath().ToString().CStr());
		return recentProjectJsonPath;
	}

	PathW EditorProjectManager::GetEngineAssetsPath()
	{
		const PathW exeDir = GetEngineResourcesDir();
		PathW recentProjectJsonPath = exeDir / L"EngineAssets";
		return recentProjectJsonPath;
	}

	bool EditorProjectManager::CreateNewProject(PathW newDirectory, const String& name)
	{
		TOwningPointer<GameStartupSettings> gameStartupSettings = CreateOwning<GameStartupSettings>();
		nlohmann::json json = TypeSerializer<TypeInfo*>::Serialize(gameStartupSettings->GetClass(), gameStartupSettings.GetRaw());
		json["projectFileVersion"] = PLU_PROJECT_VERSION;

		newDirectory += L"/";
		newDirectory += StringW::FromNarrow(name.CStr()).CStr();
		newDirectory += L"/";
		newDirectory += StringW::FromNarrow(name.CStr()).CStr();
		newDirectory += PLU_PROJECT_EXT_W;
		PLU_TRACE("New project at: {}", String::FromWide(newDirectory.CStr()).CStr());
		std::filesystem::create_directory(newDirectory.GetParentPath().CStr());
		mCurrentProjectPath = newDirectory;
		EnsureProjectStructure(newDirectory.GetParentPath());
		if (DiskManager::SaveJson(newDirectory.ToString(), json)) {
			OpenProject(mCurrentProjectPath);
			return true;
		} else {
			mCurrentProjectPath = L"";
			return false;
		}
	}

	// Turns a project *directory* into the .pluproject file inside it. Returns an empty path (and
	// logs why) when the directory holds no unambiguous project file. Prefers <dir>/<dir>.pluproject
	// — the layout CreateProject writes — before falling back to "the only .pluproject in here".
	static PathW ResolveProjectFileInDirectory(const PathW& directory)
	{
		const PathW byConvention = directory / (directory.GetFilename() + PLU_PROJECT_EXT_W);
		if (std::filesystem::exists(byConvention.CStr())) return byConvention;

		PathW onlyMatch;
		UInt32 matchCount = 0;
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(directory.CStr(), ec)) {
			if (!entry.is_regular_file()) continue;
			PathW candidate = PathW(entry.path().wstring().c_str());
			if (candidate.GetExtension() != PLU_PROJECT_EXT_W) continue;
			if (matchCount == 0) onlyMatch = candidate;
			matchCount++;
		}
		if (ec) {
			PLU_ERROR("Could not read project directory {}: {}", String::FromWide(directory.CStr()).CStr(), ec.message());
			return {};
		}
		if (matchCount == 1) return onlyMatch;

		if (matchCount == 0) {
			PLU_ERROR("No " PLU_PROJECT_EXT " file in directory {}", String::FromWide(directory.CStr()).CStr());
		} else {
			PLU_ERROR("Directory {} holds {} " PLU_PROJECT_EXT " files — pass the project file itself",
				String::FromWide(directory.CStr()).CStr(), matchCount);
		}
		return {};
	}

	bool EditorProjectManager::OpenProject(PathW projectPath)
	{
		PLU_INFO("Opening project at: {} ", String::FromWide(projectPath.CStr()).CStr());
		if (!std::filesystem::exists(projectPath.CStr()))
		{
			PLU_ERROR("Project does not exist!");
			return false;
		}
		// A project directory is accepted as well as the project file itself — --project and the
		// launcher's recent list are routinely pointed at the folder, which otherwise failed the
		// JSON load and left the caller retrying.
		if (std::filesystem::is_directory(projectPath.CStr()))
		{
			PathW resolved = ResolveProjectFileInDirectory(projectPath);
			if (resolved.IsEmpty()) return false;
			PLU_INFO("Resolved project directory to {}", String::FromWide(resolved.CStr()).CStr());
			projectPath = resolved;
		}
		EnsureProjectStructure(projectPath.GetParentPath());
		mCurrentProjectPath = projectPath;

		std::optional<JSON> projectFileJSON = DiskManager::LoadJson(projectPath.CStr());
		if (projectFileJSON.has_value()) {
			JSON json = projectFileJSON.value();
			if (!json.contains("projectFileVersion")) {
				PLU_ERROR("No project file version found!");
				goto afterProjectJSON;
			}
			if (json["projectFileVersion"].is_null())
			{
				PLU_ERROR("Project file version is NULL!");
				goto afterProjectJSON;
			}
			mProjectFileVersion = json["projectFileVersion"].get<float>();
			if (json["projectFileVersion"] < PLU_PROJECT_VERSION) {
				PLU_ERROR("Project file outdated!");
				goto afterProjectJSON;
			}
		} else {
			PLU_WARN("Project file is NULL!");
		}
		afterProjectJSON:

		// UE-style collision channels: load the project's config (or defaults) before scenes
		// initialize so PhysicsWorlds resolve profiles against it.
		if (projectFileJSON.has_value() && projectFileJSON->contains("collisionConfig"))
			ActiveCollisionConfig() = LoadCollisionConfig((*projectFileJSON)["collisionConfig"]);
		else
			ActiveCollisionConfig() = BuildDefaultCollisionConfig();
		// Physics channels — before scenes initialize, so bodies get their project channels. A
		// project without the file (new or older) gets just "Default".
		PhysicsChannelsManager::GetInstance()->LoadFromJsonFile(GetPhysicsChannelsConfigPath());

		CopyPythonBindsFile();
		//Thats bad, I need to make an event system :(
		mEditorAppContext->EditorShaderManager->ShaderCodeScan();
		mEditorAppContext->EditorAssetManager->ScanDirectory(GetEngineAssetsPath().ToString().ToNarrow(), true);
		mEditorAppContext->EditorAssetManager->ScanDirectory(GetProjectAssetsDirectory().ToString().ToNarrow());
		mEditorAppContext->EditorScenesManager->Initialize(mApplicationInfo);
		mEditorAppContext->EditorPythonManager->RunProjectScripts();
		mEditorAppContext->EditorPythonManager->InitializeScriptsWatcher();

		//To keep old configuration that works so new potencially broken info makes scripts not working
		PathW pythonAssetDictPath = GetProjectScriptsDirectory();
		pythonAssetDictPath /= GetProjectName() + L".py";
		mEditorAppContext->EditorAssetManager->ConstructPythonAssetDictionary(pythonAssetDictPath.ToString().ToNarrow());

		mEditorAppContext->EditorAssetManager->GetObjectEventDispatcher()->Subscribe("LoadAssetDescriptor", [this](void*) {
			PathW AssetDictPath = GetProjectScriptsDirectory();
			AssetDictPath /= GetProjectName() + L".py";
			mEditorAppContext->EditorAssetManager->ConstructPythonAssetDictionary(AssetDictPath.ToString().ToNarrow());
		});

		if (mProjectFileVersion >= 0.1f) {
			GameStartupSettings* gameStartupSettings = new GameStartupSettings();
			DeserializationContext* dc = mApplicationInfo->ConstructDeserializationContext();
			TypeSerializer<TypeInfo*>::Deserialize(dc, projectFileJSON, GameStartupSettings::GetStaticClass(), gameStartupSettings);
			delete dc;
			mGameStartupSettings = TOwningPointer(gameStartupSettings);
		}

		//Recent Projects
		auto recentProjectsJson = DiskManager::LoadJson(GetRecentProjectsJSONPath());
		if (recentProjectsJson.has_value()) {
			nlohmann::json json = recentProjectsJson.value();
			bool has = false;
			for (const auto& project : json["projects"]) {
				if (project == projectPath.CStr()) {
					has = true;
					break;
				}
			}
			if (!has) {
				json["projects"].push_back(projectPath.CStr());
			}
			recentProjectsJson = json;
		} else {
			nlohmann::json json;
			json["projects"] = nlohmann::json::array();
			json["projects"].push_back(projectPath.CStr());
			recentProjectsJson = json;
		}
		DiskManager::SaveJson(GetRecentProjectsJSONPath().ToString(), recentProjectsJson.value());
		mEditorAppContext->EditorPanelManager->ClosePanel(*mEditorAppContext->EditorPanelManager->GetPanelByClass(TClassPointer<EditorPanel>(ProjectLauncherPanel::GetStaticClass()))->GetEngineObjectHandle());
		mApplicationInfo->AppWindow->SetWindowTitle(GetProjectName().ToNarrow());
		if (!mGameStartupSettings)
		{
			mGameStartupSettings = CreateOwning<GameStartupSettings>();
		}
		if (mGameStartupSettings->EditorStartupScene) {
			mEditorAppContext->EditorViewportManager->CreateViewport(mApplicationInfo->AppAssetManager->GetAssetPath(mGameStartupSettings->EditorStartupScene->Uuid).ToString().ToWide(), SceneViewport::GetStaticClass());
		}
		mEditorAppContext->EditorPanelManager->AddPanel(AssetBrowserPanel::GetStaticClass());
		// The saved window layout can only be put back now: viewports are assets (nothing resolves
		// them before a project is open), and the panels this function just opened have to land in
		// their saved window instead of in whatever window happens to have focus.
		mEditorAppContext->EditorWindowsManager->RestoreLayoutAfterProjectOpen();
		mApplicationInfo->AppWindow->Maximize();
		return true;
	}

	void EditorProjectManager::EnsureProjectStructure(const PathW& projectPath)
	{
		std::filesystem::create_directory((projectPath.ToString() + L"/" + L"Assets").CStr());
		std::filesystem::create_directory((projectPath.ToString() + L"/" + L"Scripts").CStr());
		std::filesystem::create_directory((projectPath.ToString() + L"/" + L"Shaders").CStr());
		std::filesystem::create_directory((projectPath.ToString() + L"/" + L"Cache").CStr());
		std::filesystem::create_directory((projectPath.ToString() + L"/" + L"Config").CStr());
	}

	void EditorProjectManager::CopyPythonBindsFile() const
	{
		PathW bindPath = GetExePath().GetParentPath();
		bindPath /= L"PluEngine.pyi";
		std::filesystem::copy(bindPath.CStr(), GetProjectScriptsDirectory().CStr(), std::filesystem::copy_options::overwrite_existing);
	}

	PathW EditorProjectManager::GetPhysicsChannelsConfigPath() const
	{
		PathW path = GetProjectConfigDirectory();
		path /= L"PhysicsChannels.json";
		return path;
	}

	void EditorProjectManager::SavePhysicsChannels() const
	{
		if (!IsAnyProjectOpen()) return;
		PhysicsChannelsManager::GetInstance()->SaveToJsonFile(GetPhysicsChannelsConfigPath());
	}

	PathW EditorProjectManager::GetProjectConfigDirectory() const
	{
		return GetProjectDirectory().ToString() + L"Config";
	}

	PathW EditorProjectManager::GetProjectAssetsDirectory() const
	{
		return GetProjectDirectory().ToString() + L"Assets";
	}

	PathW EditorProjectManager::GetProjectScriptsDirectory() const
	{
		return GetProjectDirectory().ToString() + L"Scripts";
	}

	PathW EditorProjectManager::GetProjectShadersDirectory() const
	{
		return GetProjectDirectory().ToString() + L"Shaders";
	}

	PathW EditorProjectManager::GetProjectCacheDirectory() const
	{
		return GetProjectDirectory().ToString() + L"Cache";
	}
}
