//
// Created by Plutex on 7/6/26.
//

#include "PluEngine/AssetPipeline/Skeleton/SkeletonAssetLoader.h"

#include "PluEngine/AssetPipeline/SkeletalMesh/SkeletalMeshImporter.h"
#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

Plu::String Plu::SkeletonAssetLoader::GetSupportedAssetType()
{
    return Skeleton::GetStaticClass()->TypeName;
}

bool Plu::SkeletonAssetLoader::LoadAssetData(TUsePointer<AssetDescriptor> assetDesc,
    TOwningPointer<IAssetData> *assetDataToPopulate, TUsePointer<EngineAssetManager> assetManager,
    TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager)
{
    TOwningPointer<Skeleton> skeleton = CreateOwning<Skeleton>();
    if (!LoadSkeleton(assetDesc->AssetPath, *skeleton)) return false;
    *assetDataToPopulate = skeleton;
    return true;
}

#ifdef PLU_ENGINE_EDITOR_BUILD
Plu::TypeInfo * Plu::SkeletonAssetLoader::GetAssetTypeViewportClass()
{
    // Editor-side class, resolved by name so LibEngine stays free of an Editor include.
    return TypeRegistry::GetInstance()->GetTypeOfName("SkeletonViewport");
}

bool Plu::SkeletonAssetLoader::DispatchAssetSave(TUsePointer<AssetDescriptor> assetDesc,
    TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager,
    TUsePointer<SceneManager> sceneManager)
{
    TUsePointer<Skeleton> skeleton = assetManager->GetAssetData(assetDesc);
    if (!skeleton) return false;
    return SaveSkeleton(assetDesc->AssetPath, *skeleton);
}

bool Plu::SkeletonAssetLoader::IsAssetCreatable()
{
    return false;
}

bool Plu::SkeletonAssetLoader::CanImportAsset(Path assetPath, TUsePointer<EngineAssetManager> assetManager,
    TUsePointer<EngineObjectManager> objectManager)
{
    return false;
}
#endif
