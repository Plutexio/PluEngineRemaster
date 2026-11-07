//
// Created by Plutex on 1/20/26.
//

#ifndef PLUENGINE_EDITORSHADERMANAGER_H
#define PLUENGINE_EDITORSHADERMANAGER_H

#include "efsw/efsw.hpp"

#include "PluEngine/Render/ShadersManager.h"
#include "EditorShaderManager.generated.h"
#include "PluEngine/Render/ShaderCacheWriter.h"

namespace Plu
{
	struct MaterialInfo;
	class EditorProjectManager;
	class EditorShaderCode;

	PLU_CLASS()
	class EditorShaderWriter : public IShaderCacheWriter
	{
		REFLECTION_BODY_EDITORSHADERWRITER()
	public:
		EditorShaderWriter() = default;
		virtual ~EditorShaderWriter() override = default;
		PathW GetShaderCacheDirectory() override;
	};

	class EFSWShaderUpdateListener : public efsw::FileWatchListener
	{
		public:
		void handleFileAction(efsw::WatchID watchid, const std::string &dir, const std::string &filename, efsw::Action action, std::string oldFilename) override;
	};

	PLU_CLASS()
	class EditorShaderManager : public IShaderManager
	{
		REFLECTION_BODY_EDITORSHADERMANAGER()
	private:
		HashMap<UInt64, TOwningPointer<ShaderProgram>> mShaderPrograms;
		DynamicArray<TUsePointer<ShaderProgram>> mInitializedShaderPrograms;
		HashMap<UInt64, TOwningPointer<EditorShaderCode>> mShaderCodes;

		efsw::WatchID mEngineShadersWatchId;
		efsw::WatchID mProjectShadersWatchId;
		efsw::FileWatcher* mFileWatcher = nullptr;
		EFSWShaderUpdateListener* mListener = nullptr;

		TUsePointer<EditorProjectManager> mProjectManager;

		DynamicArray<TUsePointer<MaterialInfo>> mMaterialsToLoad;
		bool mAssetsLoaded = false;

		void ReloadMaterialUniforms(TUsePointer<ShaderProgram> program);
	public:
		EditorShaderManager();
		~EditorShaderManager() override;

		void PreInit(TUsePointer<EditorProjectManager> editorProjectManager);
		void ShaderCodeScan();
		void InitShaders();

		void PrepareShaderCodesForDistribution(Path dir);
		void CheckForShaderChanges();

		void RecompileShaderCode(TUsePointer<EditorShaderCode> shaderCode);
		void RefreshShaderUniforms(TUsePointer<ShaderProgram> program);
		void EnsureShaderInitialized(TUsePointer<ShaderProgram> program);
		void AddMaterialToLoad(TUsePointer<MaterialInfo> material);
		void HandleMaterialLoading();

		TUsePointer<IShaderCode> GetShaderCode(PluUUID uuid) override;
		bool ShaderCodeExists(PluUUID uuid) override;
		TUsePointer<ShaderProgram> GetShaderProgram(PluUUID uuid) override;
		DynamicArray<TUsePointer<ShaderProgram>> *GetRenderableShaderPrograms() override;
		void LoadShader(PluUUID uuid) override;
	};
}

#endif //PLUENGINE_EDITORSHADERMANAGER_H