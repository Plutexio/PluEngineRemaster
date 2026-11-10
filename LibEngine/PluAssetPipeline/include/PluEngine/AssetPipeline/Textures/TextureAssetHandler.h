//
// Created by Plutex on 2026-02-07.
//

#ifndef PLUENGINE_TEXTUREASSETHANDLER_H
#define PLUENGINE_TEXTUREASSETHANDLER_H


#include "PluEngine/Core.h"
#include "TextureAssetHandler.generated.h"
#include "PluEngine/AssetCore/AssetLoader.h"

namespace Plu
{
	struct IAssetData;
	struct AssetDescriptor;
	PLU_CLASS()
	class TextureAssetHandler final : public IAssetLoader
	{
		REFLECTION_BODY_TEXTUREASSETHANDLER()
	public:
		TextureAssetHandler() = default;
		~TextureAssetHandler() override = default;

#ifdef PLU_ENGINE_EDITOR_BUILD
		TypeInfo *GetAssetTypeViewportClass() override;
		DynamicArray<String> GetSupportedImportExtensions() override;
		TypeInfo *GetImportSettingsClass() override;
		void HandleAssetImporting(Path &assetPath, Path outPath, void *importSettings, TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager) override;
		bool IsAssetCreatable() override;
#endif
		String GetSupportedAssetType() override;
		bool LoadAssetData(TUsePointer<AssetDescriptor> assetDesc, TOwningPointer<IAssetData> *assetDataToPopulate, TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager, TUsePointer<IShaderManager> shaderManager) override;
	};
}

#endif //PLUENGINE_TEXTUREASSETHANDLER_H