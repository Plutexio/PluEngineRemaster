//
// Created by Plutex on 7/5/26.
//

#include "PluEngine/AssetPipeline/SkeletalMesh/SkeletalMeshAssetLoader.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

#include "PluEngine/AssetPipeline/SkeletalMesh/SkeletalMeshImporter.h"
#include "PluEngine/AssetTypes/SkeletalMesh/SkeletalMesh.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"

Plu::String Plu::SkeletalMeshAssetLoader::GetSupportedAssetType()
{
    return SkeletalMesh::GetStaticClass()->TypeName;
}

bool Plu::SkeletalMeshAssetLoader::LoadAssetData(TUsePointer<AssetDescriptor> assetDesc,
    TOwningPointer<IAssetData> *assetDataToPopulate, TUsePointer<EngineAssetManager> assetManager,
    TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager,
    TUsePointer<IShaderManager> shaderManager)
{
    TOwningPointer<SkeletalMesh> skeletalMesh = CreateOwning<SkeletalMesh>();
    if (!LoadSkeletalMesh(assetDesc->AssetPath, *skeletalMesh, assetManager))
        return false;

    *assetDataToPopulate = skeletalMesh;
    return true;
}

#ifdef PLU_ENGINE_EDITOR_BUILD
DynamicArray<Plu::String> Plu::SkeletalMeshAssetLoader::GetSupportedImportExtensions()
{
    return {".fbx", ".glb", ".gltf"};
}

Plu::TypeInfo * Plu::SkeletalMeshAssetLoader::GetImportSettingsClass()
{
    return SkeletalMeshImportOptions::GetStaticClass();
}

void Plu::SkeletalMeshAssetLoader::HandleAssetImporting(DynamicArray<Path> &assetPaths, Path outPath,
    void *importSettings, TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager)
{
    for (auto path : assetPaths) {
        ImportSkeletalMesh(path, outPath, assetManager, *static_cast<SkeletalMeshImportOptions*>(importSettings));
    }
}

Plu::TypeInfo * Plu::SkeletalMeshAssetLoader::GetAssetTypeViewportClass()
{
    return TypeRegistry::GetInstance()->GetTypeOfName("SkeletalMeshViewport");
}

bool Plu::SkeletalMeshAssetLoader::DispatchAssetSave(TUsePointer<AssetDescriptor> assetDesc,
    TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager,
    TUsePointer<SceneManager> sceneManager, TUsePointer<IShaderManager> shaderManager)
{
    TUsePointer<SkeletalMesh> skeletalMesh = assetManager->GetAssetData(assetDesc);
    if (!skeletalMesh) return false;
    return SaveSkeletalMesh(assetDesc->AssetPath, *skeletalMesh);
}

bool Plu::SkeletalMeshAssetLoader::IsAssetCreatable()
{
    return false;
}

bool Plu::SkeletalMeshAssetLoader::CanImportAsset(Path assetPath, TUsePointer<EngineAssetManager> assetManager,
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

    if (!scene || !scene->mRootNode)
    {
        PLU_CORE_ERROR("Assimp Error: {}", importer.GetErrorString());
        return false;
    }

    if (scene->HasAnimations()) {
        return true;
    }

    for (UInt32 i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        if (mesh->HasBones()) {
            return true;
        }
    }
    return false;
}
#endif
