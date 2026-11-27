//
// Created by Plutex on 1/14/26.
//

#include "TextureDisplayPanel.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/Image/ImageInfo.h"

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
		TUsePointer<ImageInfo> image = gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
		if (image) {
			// No renderer yet, so no GPU texture to show: only what the asset itself knows.
			ImGui::Text("%u x %u, %u channel(s)", image->GetWidth(), image->GetHeight(), image->GetChannels());
		}
	}
	EndPanel();
}
