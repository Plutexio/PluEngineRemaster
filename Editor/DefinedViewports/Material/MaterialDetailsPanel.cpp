//
// Created by Plutex on 1/14/26.
//

#include "MaterialDetailsPanel.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

#include "EditorAppContext.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/Material/Material.h"

extern Plu::ApplicationInfo* gApplicationInfo;
extern Plu::EditorAppContext* gEditorAppContext;

Plu::String Plu::MaterialDetailsPanel::GetPanelName()
{
	return "Properties";
}

void Plu::MaterialDetailsPanel::OnClosed()
{
}

void Plu::MaterialDetailsPanel::OnOpened()
{
}

void Plu::MaterialDetailsPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<MaterialInfo> material = gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
		if (material)
		{
			// Materials carry no data until the Vulkan material model exists.
			ImGui::TextDisabled("No material properties");
		}
	}
	EndPanel();
}
