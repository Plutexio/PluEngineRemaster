//
// Created by Plutex on 1/6/26.
//

#ifndef PLUENGINE_EDITORPROJECTMANAGER_H
#define PLUENGINE_EDITORPROJECTMANAGER_H
#include "PluEngine/Core.h"
#include "EditorProjectManager.generated.h"
#include "Path/Path.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/Objects/EngineObject.h"

#define PLU_PROJECT_VERSION 0.1

namespace Plu
{
	struct GameStartupSettings;
	struct ApplicationInfo;
	struct EditorAppContext;
	PLU_CLASS()
	class EditorProjectManager : public EngineObject
	{
		REFLECTION_BODY_EDITORPROJECTMANAGER()
	private:
		EditorAppContext* mEditorAppContext;
		ApplicationInfo* mApplicationInfo;
		PathW mCurrentProjectPath; //Path to project

		float mProjectFileVersion = 0.f;
		TOwningPointer<GameStartupSettings> mGameStartupSettings;
	public:
		EditorProjectManager();
		~EditorProjectManager() override;

		void Shutdown();

		void SetEditorAppContext(EditorAppContext* appContext, ApplicationInfo* applicationInfo);
		EditorAppContext* GetAppContext();

		[[nodiscard]] bool IsAnyProjectOpen() const;
		[[nodiscard]] PathW GetProjectDirectory() const;
		[[nodiscard]] StringW GetProjectName() const;
		[[nodiscard]]  PathW GetProjectPath() const;

		void BuildProjectForShipment(PathW dir);

		float GetProjectFileVersion() const;
		TUsePointer<GameStartupSettings> GetGameStartupSettings() const;
		static PathW GetRecentProjectsJSONPath();
		static PathW GetEngineAssetsPath();
		bool CreateNewProject(PathW newDirectory, const String& name);
		bool OpenProject(PathW projectPath);
		static void EnsureProjectStructure(const PathW& projectPath);
		void CopyPythonBindsFile() const;

		[[nodiscard]] PathW GetProjectConfigDirectory() const;
		[[nodiscard]] PathW GetProjectAssetsDirectory() const;
		[[nodiscard]] PathW GetProjectScriptsDirectory() const;
		[[nodiscard]] PathW GetProjectShadersDirectory() const;
		[[nodiscard]] PathW GetProjectCacheDirectory() const;

		// Config/PhysicsChannels.json of the open project.
		[[nodiscard]] PathW GetPhysicsChannelsConfigPath() const;
		void SavePhysicsChannels() const;

	};
}

#endif //PLUENGINE_EDITORPROJECTMANAGER_H