//
// Created by Plutex on 1/10/26.
//

#include "PluEngine/AssetPipeline/StaticMesh/StaticMeshAssetImporter.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "PluEngine/AssetPipeline/StaticMesh/StaticMeshAssimpLoader.h"
#include "glm/geometric.hpp"

#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/PluUUID.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

#ifdef PLU_ENGINE_EDITOR_BUILD
DynamicArray<Plu::String> Plu::StaticMeshAssetHandler::GetSupportedImportExtensions()
{
	return {".fbx", ".obj", ".glb", ".gltf"};
}

Plu::TypeInfo * Plu::StaticMeshAssetHandler::GetImportSettingsClass()
{
	return StaticMeshImportProps::GetStaticClass();
}

void Plu::StaticMeshAssetHandler::HandleAssetImporting(DynamicArray<Path> &assetPaths, Path outPath,
	void *importSettings, TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager)
{
	for (auto path : assetPaths) {
		MeshImporter::ImportStaticMesh(*static_cast<StaticMeshImportProps*>(importSettings), path.ToString().ToWide(), outPath.ToString().ToWide(), assetManager);
	}
}

Plu::TypeInfo * Plu::StaticMeshAssetHandler::GetAssetTypeViewportClass()
{
	return TypeRegistry::GetInstance()->GetTypeOfName("StaticMeshViewport");
}

bool Plu::StaticMeshAssetHandler::DispatchAssetSave(TUsePointer<AssetDescriptor> assetDesc,
	TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager,
	TUsePointer<SceneManager> sceneManager, TUsePointer<IShaderManager> shaderManager)
{
	TUsePointer<StaticMesh> staticMesh = assetManager->GetAssetData(assetDesc);
	if (!staticMesh) return false;
	return MeshImporter::SaveStaticMesh(assetDesc->AssetPath.ToString().ToWide(), staticMesh.GetRaw());
}

bool Plu::StaticMeshAssetHandler::IsAssetCreatable()
{
	return false;
}

bool Plu::StaticMeshAssetHandler::CanImportAsset(Path assetPath, TUsePointer<EngineAssetManager> assetManager,
	TUsePointer<EngineObjectManager> objectManager)
{
	Assimp::Importer importer;
	const aiScene* scene = nullptr;
	try {
		scene = importer.ReadFile(assetPath.CStr(), 0);
	} catch (...) {
		PLU_ERROR("Error importing mesh at: {}", assetPath.CStr());
		return false;
	}

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		PLU_CORE_ERROR("Assimp Error: {}", importer.GetErrorString());
		return false;
	}

	for (UInt32 i = 0; i < scene->mNumMeshes; i++) {
		aiMesh* mesh = scene->mMeshes[i];
		if (mesh->HasBones()) return false;
	}
	return true;
}
#endif

Plu::String Plu::StaticMeshAssetHandler::GetSupportedAssetType()
{
	return "StaticMesh";
}

bool Plu::StaticMeshAssetHandler::LoadAssetData(TUsePointer<AssetDescriptor> assetDesc,
	TOwningPointer<IAssetData> *assetDataToPopulate, TUsePointer<EngineAssetManager> assetManager,
	TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager,
	TUsePointer<IShaderManager> shaderManager)
{
	TOwningPointer<StaticMesh> staticMesh = CreateOwning<StaticMesh>();
	MeshImporter::LoadStaticMesh(assetDesc->AssetPath.ToString().ToWide(), staticMesh.GetRaw());
	*assetDataToPopulate = staticMesh;
	return true;
}
