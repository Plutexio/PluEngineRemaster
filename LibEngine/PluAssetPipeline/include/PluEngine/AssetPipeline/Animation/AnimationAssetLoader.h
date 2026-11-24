//
// Created by Plutex on 7/7/26.
//

#ifndef PLUENGINE_ANIMATIONASSETLOADER_H
#define PLUENGINE_ANIMATIONASSETLOADER_H
#include "PluEngine/Core.h"
#include "PluEngine/AssetCore/AssetLoader.h"
#include "AnimationAssetLoader.generated.h"

namespace Plu
{
    // Loader for standalone Animation binary assets (.plubin, typeName "Animation").
    // Animations are produced by the skeletal-mesh import pipeline, not imported from
    // raw source files, so this loader exposes no import extensions.
    PLU_CLASS()
    class PLUASSETPIPELINE_API AnimationAssetLoader : public IAssetLoader
    {
        REFLECTION_BODY_ANIMATIONASSETLOADER()
    public:
        AnimationAssetLoader() = default;
        virtual ~AnimationAssetLoader() override = default;

        String GetSupportedAssetType() override;
        bool LoadAssetData(TUsePointer<AssetDescriptor> assetDesc, TOwningPointer<IAssetData> *assetDataToPopulate,
                           TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager,
                           TUsePointer<SceneManager> sceneManager) override;

#ifdef PLU_ENGINE_EDITOR_BUILD
        TypeInfo *GetAssetTypeViewportClass() override;
        bool DispatchAssetSave(TUsePointer<AssetDescriptor> assetDesc, TUsePointer<EngineAssetManager> assetManager,
                               TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager) override;
        bool IsAssetCreatable() override;
        bool CanImportAsset(Path assetPath, TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager) override;
#endif
    };
}

#endif //PLUENGINE_ANIMATIONASSETLOADER_H
