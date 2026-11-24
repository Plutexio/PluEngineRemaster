//
// Created by Plutex on 7/20/26.
//

#ifndef PLUENGINE_ANIMATIONGRAPHASSETLOADER_H
#define PLUENGINE_ANIMATIONGRAPHASSETLOADER_H
#include "PluEngine/Core.h"
#include "PluEngine/AssetCore/AssetLoader.h"
#include "AnimationGraphAssetLoader.generated.h"

namespace Plu
{
    // Loader for AnimationGraph assets (.pluasset, JSON, typeName "AnimationGraph").
    // Created from the editor's asset browser rather than imported from a source file, so no
    // import extensions are exposed.
    PLU_CLASS()
    class PLUASSETPIPELINE_API AnimationGraphAssetLoader : public IAssetLoader
    {
        REFLECTION_BODY_ANIMATIONGRAPHASSETLOADER()
    public:
        AnimationGraphAssetLoader() = default;
        virtual ~AnimationGraphAssetLoader() override = default;

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

#endif //PLUENGINE_ANIMATIONGRAPHASSETLOADER_H
