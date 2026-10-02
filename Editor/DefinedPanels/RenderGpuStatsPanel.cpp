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
#include "PluEngine/Render/RenderingManager.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Render/GLFrameBuffer.h"
#include "PluEngine/Render/GLTexture.h"
#include "PluEngine/Render/RenderUsageStats.h"
#include "PluEngine/PluUtils.h"
#include "UI/IconsFontAwesome7.h"

Plu::String Plu::RenderGpuStatsPanel::GetPanelName()
{
	return ICON_FA_DISPLAY " Render / GPU";
}

void Plu::RenderGpuStatsPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel()) {
		// Live numbers, no input behind them: keep them ticking under power saving.
		RequestRedrawAfter(kStatsRefreshSeconds);
		if (ImGui::BeginTabBar("##rendergpu_tabs")) {
			if (ImGui::BeginTabItem(ICON_FA_GAUGE_HIGH " Overview")) {
				DrawOverviewTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(ICON_FA_FIRE " Hottest Assets")) {
				DrawHottestAssetsTab();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	EndPanel();
}

void Plu::RenderGpuStatsPanel::DrawOverviewTab()
{
	const float renderFPS = GetRenderThreadFPS();
	ImGui::Text("Render thread: %.1f FPS (%.2f ms)", renderFPS, renderFPS > 0.0f ? 1000.0f / renderFPS : 0.0f);
	const float mainFPS = GetMainThreadFPS();
	ImGui::TextDisabled("Main thread:   %.1f FPS (%.2f ms)", mainFPS, mainFPS > 0.0f ? 1000.0f / mainFPS : 0.0f);
	ImGui::Separator();

	if (!mApplicationInfo->AppRenderingManager) {
		ImGui::TextDisabled("No rendering manager available.");
		return;
	}

	ImGui::SeparatorText("Scene Pass");
	ImGui::Text("Draw Calls: %u", GetStatDrawCalls());
	ImGui::Text("Instances Drawn: %u", GetStatInstancesDrawn());
	ImGui::Text("Culled: %u", GetStatCulledCount());
	ImGui::SetItemTooltip("Camera frustum culling plus per-cascade shadow caster culling — one object can be counted several times when it is culled from several cascades.");
	ImGui::Separator();

	DrawShadowsSection();

	DrawSpotLightsSection();

	ImGui::SeparatorText("Triple Buffers");
	ImGui::TextDisabled("Dropped = producer outran consumer | Reused = consumer outran producer");
	TUsePointer<RenderingManager> rendering = mApplicationInfo->AppRenderingManager;
	if (ImGui::BeginTable("##triplebuffers", 3,
	                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("Buffer", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Dropped", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Reused", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Text("Scene (RenderSnapshot)");
		ImGui::TableNextColumn();
		ImGui::Text("%u", rendering->GetSnapshotDroppedCount());
		ImGui::TableNextColumn();
		ImGui::Text("%u", rendering->GetSnapshotReusedCount());

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Text("ImGui draw data");
		ImGui::TableNextColumn();
		ImGui::Text("%u", rendering->GetImGuiDroppedCount());
		ImGui::TableNextColumn();
		ImGui::Text("%u", rendering->GetImGuiReusedCount());

		ImGui::EndTable();
	}
	if (ImGui::Button(ICON_FA_ARROWS_ROTATE " Reset Counters")) {
		rendering->ResetTripleBufferTelemetry();
	}

	ImGui::SeparatorText("Main Framebuffer");
	TUsePointer<FrameBuffer> mainFb = mApplicationInfo->AppRenderingManager->RequestMainFrameBuffer();
	if (mainFb) {
		ImGui::Text("Size: %d x %d", mainFb->GetWidth(), mainFb->GetHeight());
		ImGui::Text("Color attachment: %s", mainFb->GetColorTexture() ? "yes" : "no");
		ImGui::Text("Depth attachment: %s", mainFb->GetDepthTexture() ? "yes" : "no");
		if (ImGui::Button(ICON_FA_IMAGE " View Main Framebuffer")) {
			TUsePointer<TextureViewerPanel> viewer = mEditorPanelManager->AddPanel<TextureViewerPanel>();
			viewer->FrameBufferToView = mainFb;
		}
	} else {
		ImGui::TextDisabled("Main framebuffer not available yet.");
	}
}

void Plu::RenderGpuStatsPanel::DrawShadowsSection()
{
	ImGui::SeparatorText("Shadows (Directional CSM)");

	TUsePointer<RenderingManager> rendering = mApplicationInfo->AppRenderingManager;
	const Int32 layerCount = rendering->GetShadowCascadeLayerCount();
	const UInt32 liveCascades = GetStatShadowCascadeCount();

	if (layerCount <= 0) {
		ImGui::TextDisabled("Shadow resources not created yet.");
		return;
	}

	if (liveCascades == 0) {
		ImGui::TextDisabled("No directional shadows this frame (no light, or CastShadows is off).");
	} else {
		for (UInt32 c = 0; c < liveCascades; c++) {
			ImGui::Text("Cascade %u casters: %u", c, GetStatShadowCascadeCasters(c));
		}
		ImGui::TextDisabled("Per-cascade GPU time is under \"GPU: Renderer::ShadowCascadeN\" in the Profiler panel.");
	}

	// Cascade viewer. The request has to be renewed every frame — the render thread only pays for
	// the copy while somebody is actually looking.
	//
	// The list is built from what the renderer actually has rather than hardcoded: the cascade
	// count is a per-light setting, and each cascade carries its own resolution in the atlas.
	static constexpr const char* kCascadeLabels[] = {"0", "1", "2", "3", "4", "5", "6", "7"};
	const Int32 selectableCascades = std::min(layerCount, static_cast<Int32>(std::size(kCascadeLabels)));
	mShadowViewLayer = std::clamp(mShadowViewLayer, 0, selectableCascades - 1);
	ImGui::SetNextItemWidth(120.0f);
	ImGui::Combo("Cascade", &mShadowViewLayer, kCascadeLabels, selectableCascades);

	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_IMAGE " View Cascade")) {
		mShadowViewActive = true;
	}
	if (mShadowViewActive) {
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_XMARK " Stop")) {
			mShadowViewActive = false;
			rendering->RequestShadowCascadeView(-1);
		}
	}

	if (!mShadowViewActive) return;

	rendering->RequestShadowCascadeView(mShadowViewLayer);
	TUsePointer<Texture> layerView = rendering->GetShadowCascadeView();
	if (!layerView || !layerView->IsValid()) {
		ImGui::TextDisabled("Waiting for the render thread to copy the cascade...");
		return;
	}

	// Depth textures sample as (depth, 0, 0, 1), so the preview reads as a red heightfield —
	// that is the raw depth, not a rendering bug.
	const float previewSize = std::min(ImGui::GetContentRegionAvail().x, 256.0f);
	// GL flips Y, hence the inverted UVs (same as TextureViewerPanel).
	ImGui::Image((ImTextureID)(intptr_t)layerView->GetID(), ImVec2(previewSize, previewSize), ImVec2(0, 1), ImVec2(1, 0));
	ImGui::TextDisabled("%d x %d, D32F. Red channel = raw depth.", layerView->GetWidth(), layerView->GetHeight());
}

void Plu::RenderGpuStatsPanel::DrawSpotLightsSection()
{
	ImGui::SeparatorText("Spot Lights");

	TUsePointer<RenderingManager> rendering = mApplicationInfo->AppRenderingManager;
	const Int32  slotCapacity  = rendering->GetSpotShadowSlotCount();
	const UInt32 visibleLights = GetStatVisibleSpotLights();
	const UInt32 usedSlots     = GetStatSpotShadowSlots();

	ImGui::Text("Visible this frame: %u", visibleLights);
	ImGui::SetItemTooltip("Spot lights that survived camera frustum culling on the main thread. Lights past the visible cap are dropped by importance.");

	if (slotCapacity <= 0) {
		ImGui::TextDisabled("Spot shadow atlas not created yet.");
		return;
	}

	ImGui::Text("Shadow slots used: %u / %d", usedSlots, slotCapacity);
	ImGui::SetItemTooltip("Slots are handed out per frame by importance (priority, then intensity over squared distance). A light without a slot still lights the scene — it just casts no shadow.");

	if (usedSlots == 0) {
		ImGui::TextDisabled("No spot shadows this frame (no light in view, or CastShadows is off).");
	} else {
		for (UInt32 s = 0; s < usedSlots; s++) {
			ImGui::Text("Slot %u casters: %u", s, GetStatSpotShadowCasters(s));
		}
		ImGui::TextDisabled("Per-slot GPU time is under \"GPU: Renderer::SpotShadowSlotN\" in the Profiler panel.");
	}

	// Slot viewer, same renew-every-frame contract as the cascade viewer: the render thread only
	// pays for the image copy while somebody is looking.
	ImGui::SetNextItemWidth(120.0f);
	ImGui::Combo("Slot", &mSpotShadowViewSlot, "0\0" "1\0" "2\0" "3\0" "4\0" "5\0" "6\0" "7\0\0");
	mSpotShadowViewSlot = std::clamp(mSpotShadowViewSlot, 0, slotCapacity - 1);

	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_IMAGE " View Slot")) {
		mSpotShadowViewActive = true;
	}
	if (mSpotShadowViewActive) {
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_XMARK " Stop##spotshadow")) {
			mSpotShadowViewActive = false;
			rendering->RequestSpotShadowView(-1);
		}
	}

	if (!mSpotShadowViewActive) return;

	rendering->RequestSpotShadowView(mSpotShadowViewSlot);
	TUsePointer<Texture> slotView = rendering->GetSpotShadowView();
	if (!slotView || !slotView->IsValid()) {
		ImGui::TextDisabled("Waiting for the render thread to copy the slot...");
		return;
	}

	const float previewSize = std::min(ImGui::GetContentRegionAvail().x, 256.0f);
	// GL flips Y, hence the inverted UVs (same as the cascade viewer).
	ImGui::Image((ImTextureID)(intptr_t)slotView->GetID(), ImVec2(previewSize, previewSize), ImVec2(0, 1), ImVec2(1, 0));
	ImGui::TextDisabled("%d x %d, D32F. Red channel = raw depth.", slotView->GetWidth(), slotView->GetHeight());
}

namespace {
	// Wiersz do posortowania — kopia wpisu rejestru + jego UUID.
	struct HottestRow {
		UInt64 Uuid;
		Plu::AssetUsageEntry Entry;
	};

	// Zamienia snapshot rejestru na listę posortowaną malejąco po sumarycznym użyciu.
	DynamicArray<HottestRow> SortByTotalUses(const Plu::HashMap<UInt64, Plu::AssetUsageEntry>& stats)
	{
		DynamicArray<HottestRow> rows;
		for (const auto& pair : stats) {
			rows.PushBack(HottestRow{ pair.first, pair.second });
		}
		std::sort(rows.begin(), rows.end(), [](const HottestRow& a, const HottestRow& b) {
			if (a.Entry.TotalUses != b.Entry.TotalUses) return a.Entry.TotalUses > b.Entry.TotalUses;
			return a.Entry.LastFrameUses > b.Entry.LastFrameUses;
		});
		return rows;
	}
}

Plu::String Plu::RenderGpuStatsPanel::ResolveAssetName(UInt64 uuid)
{
	if (!mApplicationInfo->AppAssetManager) return "<no asset manager>";
	TUsePointer<AssetDescriptor> desc = mApplicationInfo->AppAssetManager->GetAssetDescriptor(PluUUID(uuid));
	if (!desc) return "<unknown>";
	if (!desc->AssetName.IsEmpty()) return desc->AssetName;
	// Fallback: nazwa pliku ze ścieżki assetu, gdy AssetName jest puste.
	String path = desc->AssetPath.ToString();
	if (!path.IsEmpty()) return path;
	return "<unnamed>";
}

void Plu::RenderGpuStatsPanel::DrawHottestAssetsTab()
{
	ImGui::TextDisabled("Ranked by usage in the scene pass. Shadow textures (CSM cascade maps) are not counted.");
	ImGui::TextDisabled("This frame = instances in the last frame | Total = accumulated since reset.");
	if (ImGui::Button(ICON_FA_ARROWS_ROTATE " Reset Usage Stats")) {
		RenderUsageStats::GetInstance()->Clear();
	}
	ImGui::Separator();

	const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

	// --- Tekstury ---
	ImGui::SeparatorText(ICON_FA_IMAGE " Hottest Textures");
	{
		DynamicArray<HottestRow> rows = SortByTotalUses(RenderUsageStats::GetInstance()->GetTextureUsage());
		if (rows.Size() == 0) {
			ImGui::TextDisabled("No textures recorded yet.");
		} else if (ImGui::BeginTable("##hottest_textures", 3, tableFlags, ImVec2(0.0f, 180.0f))) {
			ImGui::TableSetupColumn("Texture", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("This frame", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();
			for (UInt32 i = 0; i < rows.Size(); i++) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s", ResolveAssetName(rows[i].Uuid).CStr());
				ImGui::TableNextColumn();
				ImGui::Text("%u", rows[i].Entry.LastFrameUses);
				ImGui::TableNextColumn();
				ImGui::Text("%llu", static_cast<unsigned long long>(rows[i].Entry.TotalUses));
			}
			ImGui::EndTable();
		}
	}

	// --- Static Meshe ---
	ImGui::SeparatorText(ICON_FA_CUBES " Hottest Static Meshes");
	{
		DynamicArray<HottestRow> rows = SortByTotalUses(RenderUsageStats::GetInstance()->GetMeshUsage());
		if (rows.Size() == 0) {
			ImGui::TextDisabled("No static meshes recorded yet.");
		} else if (ImGui::BeginTable("##hottest_meshes", 3, tableFlags, ImVec2(0.0f, 180.0f))) {
			ImGui::TableSetupColumn("Static Mesh", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("This frame", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();
			for (UInt32 i = 0; i < rows.Size(); i++) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s", ResolveAssetName(rows[i].Uuid).CStr());
				ImGui::TableNextColumn();
				ImGui::Text("%u", rows[i].Entry.LastFrameUses);
				ImGui::TableNextColumn();
				ImGui::Text("%llu", static_cast<unsigned long long>(rows[i].Entry.TotalUses));
			}
			ImGui::EndTable();
		}
	}

	// --- Skeletal Meshe ---
	ImGui::SeparatorText(ICON_FA_PERSON " Hottest Skeletal Meshes");
	{
		DynamicArray<HottestRow> rows = SortByTotalUses(RenderUsageStats::GetInstance()->GetSkeletalMeshUsage());
		if (rows.Size() == 0) {
			ImGui::TextDisabled("No skeletal meshes recorded yet.");
		} else if (ImGui::BeginTable("##hottest_skeletal_meshes", 3, tableFlags, ImVec2(0.0f, 180.0f))) {
			ImGui::TableSetupColumn("Skeletal Mesh", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("This frame", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();
			for (UInt32 i = 0; i < rows.Size(); i++) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s", ResolveAssetName(rows[i].Uuid).CStr());
				ImGui::TableNextColumn();
				ImGui::Text("%u", rows[i].Entry.LastFrameUses);
				ImGui::TableNextColumn();
				ImGui::Text("%llu", static_cast<unsigned long long>(rows[i].Entry.TotalUses));
			}
			ImGui::EndTable();
		}
	}
}

void Plu::RenderGpuStatsPanel::OnHide()
{
}

void Plu::RenderGpuStatsPanel::OnShow()
{
	SetCanClose(true);
}
