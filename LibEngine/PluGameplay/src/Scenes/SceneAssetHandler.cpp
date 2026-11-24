//
// Created by Plutex on 1/12/26.
//

#include "PluEngine/Gameplay/Scenes/SceneAssetHandler.h"
#include "json_fwd.hpp"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Core/Objects/EngineObjectHandle.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"

Plu::String Plu::SceneAssetHandler::GetSupportedAssetType()
{
	return SceneInfo::GetStaticClass()->TypeName;
}

bool Plu::SceneAssetHandler::LoadAssetData(TUsePointer<AssetDescriptor> assetDesc,
	TOwningPointer<IAssetData> *assetDataToPopulate, TUsePointer<EngineAssetManager> assetManager,
	TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager)
{
	TOwningPointer<SceneInfo> sceneInfo = CreateOwning<SceneInfo>();
	sceneInfo->URL = assetDesc->AssetPath.GetStem();
	std::optional<JSON> j = DiskManager::LoadJson(assetDesc->AssetPath.ToString().ToWide());
	if (!j.has_value()) return false;
	sceneInfo->Uuid = j.value()["uuid"].get<UInt64>();
	*assetDataToPopulate = sceneInfo;
	TUsePointer<SceneManager> editorScenesManager = DynamicCast<SceneManager>(sceneManager);
	editorScenesManager->RegisterSceneInfo(sceneInfo);
	return true;
}

#ifdef PLU_ENGINE_EDITOR_BUILD

Plu::TypeInfo * Plu::SceneAssetHandler::GetAssetTypeViewportClass()
{
	return TypeRegistry::GetInstance()->GetTypeOfName("SceneViewport");
}

bool Plu::SceneAssetHandler::DispatchAssetSave(TUsePointer<AssetDescriptor> assetDesc,
	TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager,
	TUsePointer<SceneManager> sceneManager)
{
	sceneManager->SaveActiveScene();
	return true;
}
#endif
