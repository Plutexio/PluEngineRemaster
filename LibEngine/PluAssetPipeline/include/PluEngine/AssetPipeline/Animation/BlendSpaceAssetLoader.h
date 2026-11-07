//
// Created by Plutex on 10/14/26.
//

#ifndef PLUENGINE_BLENDSPACEASSETLOADER_H
#define PLUENGINE_BLENDSPACEASSETLOADER_H
#include "PluEngine/Core.h"
#include "PluEngine/AssetCore/AssetLoader.h"
#include "BlendSpaceAssetLoader.generated.h"

namespace Plu
{
    // Loader for BlendSpace assets (.pluasset, JSON, typeName "BlendSpace"). The data itself is plain
    // reflection (generic JSON both ways); the loader exists to rebuild the derived triangulation
    // after load and to tie the type to its editor viewport (also on layout restore, which only asks
    // loaders — see EditorWindowsManager).
    PLU_CLASS()
    class PLUASSETPIPELINE_API BlendSpaceAssetLoader : public IAssetLoader
    {
        REFLECTION_BODY_BLENDSPACEASSETLOADER()
    public:
        BlendSpaceAssetLoader() = default;
        virtual ~BlendSpaceAssetLoader() override = default;

        String GetSupportedAssetType() override;
        bool LoadAssetData(TUsePointer<AssetDescriptor> assetDesc, TOwningPointer<IAssetData> *assetDataToPopulate,
                           TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager,
                           TUsePointer<SceneManager> sceneManager,
                           TUsePointer<IShaderManager> shaderManager) override;

#ifdef PLU_ENGINE_EDITOR_BUILD
        TypeInfo *GetAssetTypeViewportClass() override;
        bool DispatchAssetSave(TUsePointer<AssetDescriptor> assetDesc, TUsePointer<EngineAssetManager> assetManager,
                               TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager,
                               TUsePointer<IShaderManager> shaderManager) override;
        bool IsAssetCreatable() override;
        bool CanImportAsset(Path assetPath, TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager) override;
#endif
    };
}

#endif //PLUENGINE_BLENDSPACEASSETLOADER_H
