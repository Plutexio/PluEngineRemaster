//
// Created by Plutex on 1/14/26.
//

#include "TextureDisplayPanel.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/Material/Material.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"
#include "PluEngine/AssetTypes/Texture/Texture.h"
#include "PluEngine/Render/RenderingManager.h"
#include "PluEngine/Render/GLTexture.h"
#include "PluEngine/AssetTypes/ShaderCode.h"

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
			TUsePointer<Texture> texture = gApplicationInfo->AppRenderingManager->GetTextureForInfo(textureInfo);
			if (!texture) {
				gApplicationInfo->AppRenderingManager->RequestTextureFromInfo(textureInfo);
			} else {
				ImVec2 viewportSize = ImGui::GetContentRegionAvail();

				float availW = viewportSize.x;
				float availH = viewportSize.y;

				float texAspect = (float)textureInfo->RawData.Width / (float)textureInfo->RawData.Height;
				float availAspect = availW / availH;

				ImVec2 imageSize;

				// Dopasowanie bez zniekształcenia:
				if (availAspect > texAspect) {
					// Obszar UI jest bardziej poziomy → ograniczamy wysokość
					imageSize.y = availH;
					imageSize.x = imageSize.y * texAspect;
				} else {
					// Obszar UI jest bardziej pionowy → ograniczamy szerokość
					imageSize.x = availW;
					imageSize.y = imageSize.x / texAspect;
				}

				// Uzyskaj ID tekstury (ważne: to musi być zwykła tekstura, nie multisample!)
				GLuint texID = texture->GetID();

				// ImGui chce "ImTextureID"
				ImTextureID imguiTex = (ImTextureID)(intptr_t)texID;

				// Uwaga: OpenGL odwraca oś Y → dlatego UV są odwrotnie.
				ImGui::Image(imguiTex, imageSize, ImVec2(0,1), ImVec2(1,0));
			}
		}
	}
	EndPanel();
}
