//
// Created by Plutex on 10/14/26.
//

#include "BlendSpaceCanvasPanel.h"

#include <cstdio>

#include "BlendSpaceDrawing.h"
#include "BlendSpaceViewport.h"
#include "PluEngine/Application.h"
#include "PluEngine/FrameDemand.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetTypes/Animation/BlendSpace.h"
#include "UI/IconsFontAwesome7.h"

extern Plu::ApplicationInfo* gApplicationInfo;

namespace
{
	constexpr float kSampleRadius = 6.0f;
	constexpr float kHitRadius = 10.0f;
	constexpr float kCanvasMargin = 28.0f; // room for the axis labels around the plot
	constexpr float kStripHeight = 56.0f;  // 1D plot height

	Plu::String SampleLabel(const Plu::BlendSample& sample)
	{
		using namespace Plu;
		if (!sample.AnimationAsset) return "(no animation)";
		if (auto descriptor = gApplicationInfo->AppAssetManager->GetAssetDescriptor(sample.AnimationAsset->Uuid))
			return descriptor->AssetName;
		return "(animation)";
	}

	// Nearest grid line of `axis` (GridDivisions cells across the range).
	float SnapToGrid(float value, const Plu::BlendSpaceAxis& axis)
	{
		if (axis.GridDivisions <= 0) return value;
		const float step = (axis.Max - axis.Min) / static_cast<float>(axis.GridDivisions);
		if (std::abs(step) < 1e-6f) return value;
		return axis.Min + std::round((value - axis.Min) / step) * step;
	}

	void DrawGrid(ImDrawList* drawList, const Plu::BlendSpaceScreenMapping& mapping)
	{
		using namespace Plu;
		const BlendSpace& space = *mapping.Space;
		const ImU32 labelColor = IM_COL32(150, 150, 160, 255);
		char text[32];

		const int divisionsX = std::max(space.AxisX.GridDivisions, 1);
		for (int i = 0; i <= divisionsX; ++i) {
			const float value = space.AxisX.Min + (space.AxisX.Max - space.AxisX.Min) * static_cast<float>(i) / divisionsX;
			const float x = mapping.ToScreen(Vec2(value, space.AxisY.Min)).x;
			drawList->AddLine(ImVec2(x, mapping.Min.y), ImVec2(x, mapping.Max.y), BlendSpaceColors::GridLine);
			std::snprintf(text, sizeof(text), "%g", value);
			const float width = ImGui::CalcTextSize(text).x;
			drawList->AddText(ImVec2(x - width * 0.5f, mapping.Max.y + 2.0f), labelColor, text);
		}
		if (space.IsTwoD()) {
			const int divisionsY = std::max(space.AxisY.GridDivisions, 1);
			for (int i = 0; i <= divisionsY; ++i) {
				const float value = space.AxisY.Min + (space.AxisY.Max - space.AxisY.Min) * static_cast<float>(i) / divisionsY;
				const float y = mapping.ToScreen(Vec2(space.AxisX.Min, value)).y;
				drawList->AddLine(ImVec2(mapping.Min.x, y), ImVec2(mapping.Max.x, y), BlendSpaceColors::GridLine);
				std::snprintf(text, sizeof(text), "%g", value);
				const float width = ImGui::CalcTextSize(text).x;
				drawList->AddText(ImVec2(mapping.Min.x - width - 4.0f, y - ImGui::GetTextLineHeight() * 0.5f), labelColor, text);
			}
		}

		// Axis names: X centred under its numbers, Y above the plot's top-left corner.
		const float nameWidth = ImGui::CalcTextSize(space.AxisX.Name.CStr()).x;
		drawList->AddText(ImVec2((mapping.Min.x + mapping.Max.x - nameWidth) * 0.5f,
		                         mapping.Max.y + ImGui::GetTextLineHeight() + 2.0f),
		                  IM_COL32(200, 200, 210, 255), space.AxisX.Name.CStr());
		if (space.IsTwoD())
			drawList->AddText(ImVec2(mapping.Min.x, mapping.Min.y - ImGui::GetTextLineHeight() - 4.0f),
			                  IM_COL32(200, 200, 210, 255), space.AxisY.Name.CStr());
	}
}

Plu::String Plu::BlendSpaceCanvasPanel::GetPanelName()
{
	return "Samples";
}

void Plu::BlendSpaceCanvasPanel::OnClosed()
{
}

void Plu::BlendSpaceCanvasPanel::OnOpened()
{
}

void Plu::BlendSpaceCanvasPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<BlendSpaceViewport> viewport = DynamicCast<BlendSpaceViewport>(GetParentViewport());
		TUsePointer<BlendSpace> spacePtr = viewport ? viewport->GetBlendSpace() : nullptr;
		if (!viewport || !spacePtr) {
			ImGui::TextDisabled("Blend space not loaded.");
			EndPanel();
			return;
		}
		BlendSpace& space = *spacePtr;
		if (viewport->SelectedSample >= static_cast<Int64>(space.Samples.Size())) viewport->SelectedSample = -1;

		ImGui::TextDisabled("Drag samples (Shift: no snap) - click/drag empty space or Ctrl+drag: preview - right-click: add / sample menu");

		// Plot rectangle: the whole panel minus label margins; a centred strip for 1D.
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImVec2 canvasSize(std::max(available.x, 120.0f), std::max(available.y, 80.0f));
		ImGui::InvisibleButton("##BlendSpaceCanvas", canvasSize,
		                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const bool hovered = ImGui::IsItemHovered();

		ImVec2 plotMin(origin.x + kCanvasMargin + 16.0f, origin.y + kCanvasMargin);
		ImVec2 plotMax(origin.x + canvasSize.x - kCanvasMargin, origin.y + canvasSize.y - kCanvasMargin - 12.0f);
		if (!space.IsTwoD()) {
			const float centerY = (plotMin.y + plotMax.y) * 0.5f;
			plotMin.y = centerY - kStripHeight * 0.5f;
			plotMax.y = centerY + kStripHeight * 0.5f;
		}
		const BlendSpaceScreenMapping mapping{ &space, plotMin, plotMax };

		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const Vec2 mouseValue = space.ClampInput(mapping.FromScreen(mouse));

		// Sample under the cursor (nearest within the hit radius).
		Int64 hoveredSample = -1;
		float bestDistance = kHitRadius;
		for (UInt64 i = 0; i < space.Samples.Size(); ++i) {
			const ImVec2 p = mapping.ToScreen(space.Samples[i].Position);
			const float distance = std::sqrt((p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y));
			if (distance < bestDistance) { bestDistance = distance; hoveredSample = static_cast<Int64>(i); }
		}

		// --- Interaction ---------------------------------------------------------------------
		const bool ctrl = ImGui::GetIO().KeyCtrl;
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			if (hoveredSample >= 0 && !ctrl) {
				viewport->SelectedSample = hoveredSample;
				mDragMode = EDragMode::Sample;
			} else {
				if (hoveredSample < 0) viewport->SelectedSample = -1;
				mDragMode = EDragMode::Preview;
			}
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) mDragMode = EDragMode::None;

		if (mDragMode == EDragMode::Preview) {
			viewport->PreviewInput = mouseValue;
		} else if (mDragMode == EDragMode::Sample && viewport->SelectedSample >= 0) {
			BlendSample& sample = space.Samples[viewport->SelectedSample];
			Vec2 position = mouseValue;
			if (!ImGui::GetIO().KeyShift) {
				position.x = SnapToGrid(position.x, space.AxisX);
				if (space.IsTwoD()) position.y = SnapToGrid(position.y, space.AxisY);
			}
			if (position != sample.Position) {
				sample.Position = position;
				space.RebuildTriangulation();
				PanelChangedAsset();
			}
		}

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			if (hoveredSample >= 0) {
				mContextSample = hoveredSample;
				viewport->SelectedSample = hoveredSample;
				ImGui::OpenPopup("##BlendSampleMenu");
			} else {
				mPendingAddPosition = mouseValue;
				if (!ImGui::GetIO().KeyShift) {
					mPendingAddPosition.x = SnapToGrid(mPendingAddPosition.x, space.AxisX);
					if (space.IsTwoD()) mPendingAddPosition.y = SnapToGrid(mPendingAddPosition.y, space.AxisY);
				}
				ImGui::OpenPopup("##BlendCanvasMenu");
			}
		}

		if (ImGui::BeginPopup("##BlendCanvasMenu")) {
			if (ImGui::MenuItem(ICON_FA_PLUS " Add Sample Here")) {
				BlendSample sample;
				sample.Position = mPendingAddPosition;
				space.Samples.PushBack(sample);
				viewport->SelectedSample = static_cast<Int64>(space.Samples.Size()) - 1;
				space.RebuildTriangulation();
				PanelChangedAsset();
			}
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopup("##BlendSampleMenu")) {
			if (mContextSample >= 0 && mContextSample < static_cast<Int64>(space.Samples.Size())) {
				BlendSample& sample = space.Samples[mContextSample];
				ImGui::TextDisabled("%s", SampleLabel(sample).CStr());
				ImGui::Separator();
				if (ImGui::MenuItem("Exclude From Sync", nullptr, sample.ExcludeFromSync)) {
					sample.ExcludeFromSync = !sample.ExcludeFromSync;
					space.RebuildTriangulation(); // bumps the revision the preview watches
					PanelChangedAsset();
				}
				if (ImGui::MenuItem(ICON_FA_TRASH " Delete Sample")) {
					space.Samples.RemoveAt(static_cast<UInt64>(mContextSample));
					viewport->SelectedSample = -1;
					mContextSample = -1;
					space.RebuildTriangulation();
					PanelChangedAsset();
				}
			}
			ImGui::EndPopup();
		}

		// --- Drawing -------------------------------------------------------------------------
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(origin, ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y), BlendSpaceColors::Background);
		DrawGrid(drawList, mapping);

		// Blend at the preview cursor: highlight the samples in play, scaled by weight.
		DynamicArray<BlendSampleWeight> weights;
		space.ComputeWeights(viewport->PreviewInput, weights);
		if (space.IsTwoD() && weights.Size() == 3) {
			drawList->AddTriangleFilled(mapping.ToScreen(space.Samples[weights[0].SampleIndex].Position),
			                            mapping.ToScreen(space.Samples[weights[1].SampleIndex].Position),
			                            mapping.ToScreen(space.Samples[weights[2].SampleIndex].Position),
			                            IM_COL32(110, 230, 120, 40));
		}
		DrawBlendSpaceStructure(drawList, mapping, kSampleRadius);
		for (const BlendSampleWeight& weight : weights) {
			const ImVec2 p = mapping.ToScreen(space.Samples[weight.SampleIndex].Position);
			drawList->AddCircle(p, kSampleRadius + 3.0f + 8.0f * weight.Weight, BlendSpaceColors::Preview, 20, 2.0f);
		}

		for (UInt64 i = 0; i < space.Samples.Size(); ++i) {
			const BlendSample& sample = space.Samples[i];
			const ImVec2 p = mapping.ToScreen(sample.Position);
			if (static_cast<Int64>(i) == viewport->SelectedSample)
				drawList->AddCircle(p, kSampleRadius + 2.0f, BlendSpaceColors::Selected, 16, 2.0f);
			const String label = SampleLabel(sample);
			// 1D labels alternate above/below so neighbours don't overlap.
			const float labelY = space.IsTwoD() || i % 2 == 0
				? p.y - kSampleRadius - ImGui::GetTextLineHeight() - 2.0f
				: p.y + kSampleRadius + 2.0f;
			drawList->AddText(ImVec2(p.x + kSampleRadius, labelY), IM_COL32(220, 220, 225, 255), label.CStr());
		}

		// Preview cursor crosshair.
		const ImVec2 cursor = mapping.ToScreen(viewport->PreviewInput);
		drawList->AddLine(ImVec2(cursor.x - 8.0f, cursor.y), ImVec2(cursor.x + 8.0f, cursor.y), BlendSpaceColors::Preview, 2.0f);
		drawList->AddLine(ImVec2(cursor.x, cursor.y - 8.0f), ImVec2(cursor.x, cursor.y + 8.0f), BlendSpaceColors::Preview, 2.0f);
		drawList->AddRect(mapping.Min, mapping.Max, BlendSpaceColors::Border);

		// --- Readouts ------------------------------------------------------------------------
		if (mDragMode == EDragMode::Preview || (hovered && hoveredSample < 0 && ctrl)) {
			String text;
			char line[96];
			if (space.IsTwoD())
				std::snprintf(line, sizeof(line), "%s %.2f, %s %.2f", space.AxisX.Name.CStr(), viewport->PreviewInput.x,
				              space.AxisY.Name.CStr(), viewport->PreviewInput.y);
			else
				std::snprintf(line, sizeof(line), "%s %.2f", space.AxisX.Name.CStr(), viewport->PreviewInput.x);
			text += line;
			for (const BlendSampleWeight& weight : weights) {
				std::snprintf(line, sizeof(line), "\n%3.0f%%  %s", weight.Weight * 100.0f,
				              SampleLabel(space.Samples[weight.SampleIndex]).CStr());
				text += line;
			}
			ImGui::SetTooltip("%s", text.CStr());
		} else if (hovered && hoveredSample >= 0) {
			const BlendSample& sample = space.Samples[hoveredSample];
			const char* syncNote = sample.ExcludeFromSync ? "\nexcluded from sync" : "";
			if (space.IsTwoD())
				ImGui::SetTooltip("%s\n(%.2f, %.2f)%s", SampleLabel(sample).CStr(), sample.Position.x, sample.Position.y, syncNote);
			else
				ImGui::SetTooltip("%s\n%.2f%s", SampleLabel(sample).CStr(), sample.Position.x, syncNote);
		}
	}
	EndPanel();
}
