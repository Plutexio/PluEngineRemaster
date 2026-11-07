//
// Created by Plutex on 10/14/26.
//

#include "PluEngine/AssetPipeline/Animation/BlendSpaceAssetLoader.h"

#include "PluEngine/AssetTypes/Animation/BlendSpace.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Core/Reflection/ReflectionBase.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"

Plu::String Plu::BlendSpaceAssetLoader::GetSupportedAssetType()
{
    return BlendSpace::GetStaticClass()->TypeName;
}

bool Plu::BlendSpaceAssetLoader::LoadAssetData(TUsePointer<AssetDescriptor> assetDesc,
    TOwningPointer<IAssetData> *assetDataToPopulate, TUsePointer<EngineAssetManager> assetManager,
    TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager,
    TUsePointer<IShaderManager> shaderManager)
{
    std::optional<JSON> jsonOpt = DiskManager::LoadJson(assetDesc->AssetPath.ToString().ToWide());
    if (!jsonOpt.has_value()) {
        PLU_CORE_ERROR("Failed to read BlendSpace JSON at {}", assetDesc->AssetPath.ToString().CStr());
        return false;
    }

    DeserializationContext dc;
    dc.assetManager  = assetManager;
    dc.scenesManager = sceneManager;
    dc.shaderManager = shaderManager;

    auto* blendSpace = static_cast<BlendSpace*>(BlendSpace::GetStaticClass()->Construct());
    if (!blendSpace) return false;
    TypeSerializer<TypeInfo*>::Deserialize(&dc, jsonOpt.value(), BlendSpace::GetStaticClass(), blendSpace);
    // Sort order / triangulation are derived, never serialized.
    blendSpace->RebuildTriangulation();

    *assetDataToPopulate = TOwningPointer(static_cast<IAssetData*>(blendSpace));
    return true;
}

#ifdef PLU_ENGINE_EDITOR_BUILD
Plu::TypeInfo * Plu::BlendSpaceAssetLoader::GetAssetTypeViewportClass()
{
    // Editor-side class, resolved by name so LibEngine stays free of an Editor include.
    return TypeRegistry::GetInstance()->GetTypeOfName("BlendSpaceViewport");
}

bool Plu::BlendSpaceAssetLoader::DispatchAssetSave(TUsePointer<AssetDescriptor> assetDesc,
    TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager,
    TUsePointer<SceneManager> sceneManager, TUsePointer<IShaderManager> shaderManager)
{
    // Everything persistent is reflected — let EngineAssetManager's generic JSON path write it.
    return false;
}

bool Plu::BlendSpaceAssetLoader::IsAssetCreatable()
{
    // Shows up under "Create Asset" in the asset browser.
    return true;
}

bool Plu::BlendSpaceAssetLoader::CanImportAsset(Path assetPath, TUsePointer<EngineAssetManager> assetManager,
    TUsePointer<EngineObjectManager> objectManager)
{
    // Authored in the editor, never imported from a source file.
    return false;
}
#endif
