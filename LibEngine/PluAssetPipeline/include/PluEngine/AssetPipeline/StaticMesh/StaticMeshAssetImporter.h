//
// Created by Plutex on 1/10/26.
//

#ifndef PLUENGINE_STATICMESHASSETIMPORTER_H
#define PLUENGINE_STATICMESHASSETIMPORTER_H


#include "StaticMeshAssetImporter.generated.h"
#include "PluSTL/Path/Path.h"
#include "PluEngine/AssetCore/AssetLoader.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"

namespace Plu
{
	struct EditorMeshData : MeshData
	{
		Plu::String Name;
		PluUUID uuid;
	};

	struct MeshImportOptions
	{
		bool MergeMeshes = true;
	};

	PLU_CLASS()
	class StaticMeshAssetHandler : public IAssetLoader
	{
		REFLECTION_BODY_STATICMESHASSETHANDLER()
	public:
		StaticMeshAssetHandler() = default;
		virtual ~StaticMeshAssetHandler() override = default;

		String GetSupportedAssetType() override;
		bool LoadAssetData(TUsePointer<AssetDescriptor> assetDesc, TOwningPointer<IAssetData> *assetDataToPopulate,
						   TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager,
						   TUsePointer<SceneManager> sceneManager,
						   TUsePointer<IShaderManager> shaderManager) override;

#ifdef PLU_ENGINE_EDITOR_BUILD
		DynamicArray<String> GetSupportedImportExtensions() override;
		TypeInfo *GetImportSettingsClass() override;
		void HandleAssetImporting(DynamicArray<Path> &assetPaths, Path outPath, void *importSettings, TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager) override;
		TypeInfo *GetAssetTypeViewportClass() override;
		bool DispatchAssetSave(TUsePointer<AssetDescriptor> assetDesc, TUsePointer<EngineAssetManager> assetManager,
							   TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager,
							   TUsePointer<IShaderManager> shaderManager) override;
		bool IsAssetCreatable() override;
		bool CanImportAsset(Path assetPath, TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager) override;
#endif
	};
}

#endif //PLUENGINE_STATICMESHASSETIMPORTER_H