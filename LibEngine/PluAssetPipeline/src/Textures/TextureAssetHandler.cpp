//
// Created by Plutex on 2026-02-07.
//

#include "PluEngine/AssetPipeline/Textures/TextureAssetHandler.h"
#include "PluEngine/AssetPipeline/Textures/TextureImporter.h"
#include "PluEngine/PluPaths.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"
#include "PluEngine/AssetTypes/Texture/Texture.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"

#ifdef PLU_ENGINE_EDITOR_BUILD

Plu::TypeInfo * Plu::TextureAssetHandler::GetAssetTypeViewportClass()
{
	return TypeRegistry::GetInstance()->GetTypeOfName("TextureViewport");
}

DynamicArray<Plu::String> Plu::TextureAssetHandler::GetSupportedImportExtensions()
{
	return  {".png"};
}

Plu::TypeInfo * Plu::TextureAssetHandler::GetImportSettingsClass()
{
	return nullptr;
}

void Plu::TextureAssetHandler::HandleAssetImporting(Path &assetPath, Path outPath, void *importSettings,
	TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager)
{
	Path outPathForAsset = outPath;
	outPathForAsset += String::Format("/{0}{1}", assetPath.GetStem().CStr(), PLU_BINARY_EXT);
	TextureImport::ImportTexture(assetPath.ToString().ToWide(), outPathForAsset.ToString().ToWide());
	assetManager->LoadAssetDescriptor(outPathForAsset);
}

bool Plu::TextureAssetHandler::IsAssetCreatable()
{
	return false;
}

#endif

Plu::String Plu::TextureAssetHandler::GetSupportedAssetType()
{
	return "TextureInfo";
}

bool Plu::TextureAssetHandler::LoadAssetData(TUsePointer<AssetDescriptor> assetDesc,
	TOwningPointer<IAssetData> *assetDataToPopulate, TUsePointer<EngineAssetManager> assetManager,
	TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager,
	TUsePointer<IShaderManager> shaderManager)
{
	TOwningPointer<TextureInfo> textureInfo = CreateOwning<TextureInfo>();
	TextureImport::LoadTexture(assetDesc->AssetPath.ToString().ToWide(), textureInfo.GetRaw());
	*assetDataToPopulate = textureInfo;
	return true;
}
