//
// Created by Plutex on 1/14/26.
//

#include "TextureDisplayPanel.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/Texture/Texture.h"

extern Plu::ApplicationInfo* gApplicationInfo;

Plu::String Plu::TextureDisplayPanel::GetPanelName()
{
	return "Properties";
}

void Plu::TextureDisplayPanel::OnClosed()
{
}

void Plu::TextureDisplayPanel::OnOpened()
{
}

void Plu::TextureDisplayPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<TextureInfo> textureInfo = gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
		if (textureInfo) {
			// No renderer yet, so no GPU texture to show: only what the asset itself knows.
			ImGui::Text("%d x %d", textureInfo->RawData.Width, textureInfo->RawData.Height);
		}
	}
	EndPanel();
}
