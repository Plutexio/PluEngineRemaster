//
// Created by Plutex on 1/12/26.
//

#ifndef PLUENGINE_SCENEASSETHANDLER_H
#define PLUENGINE_SCENEASSETHANDLER_H
#include "PluEngine/Core.h"
#include "SceneAssetHandler.generated.h"
#include "PluEngine/AssetCore/AssetLoader.h"

namespace Plu
{
	PLU_CLASS()
	class SceneAssetHandler : public IAssetLoader
	{
		REFLECTION_BODY_SCENEASSETHANDLER()
	public:
		SceneAssetHandler() = default;
		~SceneAssetHandler() override = default;

		String GetSupportedAssetType() override;
		bool LoadAssetData(TUsePointer<AssetDescriptor> assetDesc, TOwningPointer<IAssetData> *assetDataToPopulate,
		                   TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager,
		                   TUsePointer<SceneManager> sceneManager) override;
#ifdef PLU_ENGINE_EDITOR_BUILD
		TypeInfo *GetAssetTypeViewportClass() override;
		bool DispatchAssetSave(TUsePointer<AssetDescriptor> assetDesc, TUsePointer<EngineAssetManager> assetManager,
		                       TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager) override;
#endif
	};
}

#endif //PLUENGINE_SCENEASSETHANDLER_H