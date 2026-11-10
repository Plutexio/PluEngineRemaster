//
// Created by Plutex on 5/22/26.
//

#ifndef PLUENGINE_ASSETLOADER_H
#define PLUENGINE_ASSETLOADER_H
#include "PluEngine/Core.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "AssetLoader.generated.h"

namespace Plu
{
    struct IAssetData;
    struct AssetDescriptor;
    PLU_CLASS(Abstract)
    class PLUASSETCORE_API IAssetLoader : public EngineObject
    {
        REFLECTION_BODY_IASSETLOADER()
    protected:

    public:
        virtual String GetSupportedAssetType() = 0;

        //Gives all the possible managers, bool return for optional fail information.
        virtual bool LoadAssetData(
            TUsePointer<AssetDescriptor> assetDesc,
            TOwningPointer<IAssetData>* assetDataToPopulate,
            TUsePointer<EngineAssetManager> assetManager,
            TUsePointer<EngineObjectManager> objectManager,
            TUsePointer<SceneManager> sceneManager,
            TUsePointer<IShaderManager> shaderManager
        ) = 0;

        void DispatchAssetLoad(
            TUsePointer<AssetDescriptor> assetDesc,
            TUsePointer<EngineAssetManager> assetManager,
            TUsePointer<EngineObjectManager> objectManager,
            TUsePointer<SceneManager> sceneManager,
            TUsePointer<IShaderManager> shaderManager
        );

#ifdef PLU_ENGINE_EDITOR_BUILD
        virtual TypeInfo* GetAssetTypeViewportClass() = 0;
        virtual DynamicArray<String> GetSupportedImportExtensions() {return DynamicArray<String>();}
        virtual TypeInfo* GetImportSettingsClass() {return nullptr;}
        virtual void HandleAssetImporting(Path& assetPath, Path outPath, void* importSettings, TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager) {};
        virtual bool DispatchAssetSave(
            TUsePointer<AssetDescriptor> assetDesc,
            TUsePointer<EngineAssetManager> assetManager,
            TUsePointer<EngineObjectManager> objectManager,
            TUsePointer<SceneManager> sceneManager,
            TUsePointer<IShaderManager> shaderManager
        ) {return false;}
        virtual bool IsAssetCreatable() {return true;}
        virtual bool CanImportAsset(Path assetPath, TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager) {return true;}
#endif
    };
}

#endif //PLUENGINE_ASSETLOADER_H
