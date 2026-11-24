//
// Created by Plutex on 2026-06-26.
//

#include "RenderGpuStatsPanel.h"
#include "PluEngine/FrameDemand.h"

#include <algorithm>
#include <iterator>

#include "imgui.h"
#include "Panels/EditorPanelManager.h"
#include "TextureViewerPanel.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/PluUtils.h"
#include "UI/IconsFontAwesome7.h"

Plu::String Plu::RenderGpuStatsPanel::GetPanelName()
{
	return ICON_FA_DISPLAY " Render / GPU";
}

void Plu::RenderGpuStatsPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel()) {

	}
	EndPanel();
}

void Plu::RenderGpuStatsPanel::OnHide()
{
}

void Plu::RenderGpuStatsPanel::OnShow()
{
	SetCanClose(true);
}
