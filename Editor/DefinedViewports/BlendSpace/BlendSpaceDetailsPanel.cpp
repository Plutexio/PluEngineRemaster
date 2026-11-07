//
// Created by Plutex on 10/14/26.
//

#include "BlendSpaceDetailsPanel.h"

#include <cstdio>

#include "BlendSpaceViewport.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetTypes/Animation/BlendSpace.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "UI/IconsFontAwesome7.h"

extern Plu::ApplicationInfo* gApplicationInfo;

Plu::String Plu::BlendSpaceDetailsPanel::GetPanelName()
{
	return "Details";
}

void Plu::BlendSpaceDetailsPanel::OnClosed()
{
}

void Plu::BlendSpaceDetailsPanel::OnOpened()
{
}

void Plu::BlendSpaceDetailsPanel::OnUpdate(float deltaTime)
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
		bool changed = false;

		if (ImGui::CollapsingHeader("Blend Space", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= TypeSerializer<TUsePointer<Skeleton>>::EditorControl(&space.TargetSkeleton, "Target Skeleton");
			changed |= TypeSerializer<TUsePointer<SkeletalMesh>>::EditorControl(&space.PreviewMesh, "Preview Mesh");
			if (TypeSerializer<EBlendSpaceDimensions>::EditorControl(&space.Dimensions, "Dimensions")) {
				changed = true;
				// Keep the cursor inside the new space (a 1D space has no Y).
				viewport->PreviewInput = space.ClampInput(viewport->PreviewInput);
			}
		}

		const auto drawAxis = [&](BlendSpaceAxis& axis, const char* id) {
			ImGui::PushID(id);
			bool axisChanged = TypeSerializer<String>::EditorControl(&axis.Name, "Name");
			axisChanged |= ImGui::DragFloat("Min", &axis.Min, 0.05f);
			axisChanged |= ImGui::DragFloat("Max", &axis.Max, 0.05f);
			axisChanged |= ImGui::SliderInt("Grid Divisions", &axis.GridDivisions, 1, 32);
			ImGui::PopID();
			return axisChanged;
		};
		if (ImGui::CollapsingHeader("Axis X", ImGuiTreeNodeFlags_DefaultOpen))
			changed |= drawAxis(space.AxisX, "AxisX");
		if (space.IsTwoD() && ImGui::CollapsingHeader("Axis Y", ImGuiTreeNodeFlags_DefaultOpen))
			changed |= drawAxis(space.AxisY, "AxisY");

		if (ImGui::CollapsingHeader("Samples", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (UInt64 i = 0; i < space.Samples.Size(); ++i) {
				const BlendSample& sample = space.Samples[i];
				String label = "(no animation)";
				if (sample.AnimationAsset) {
					if (auto descriptor = gApplicationInfo->AppAssetManager->GetAssetDescriptor(sample.AnimationAsset->Uuid))
						label = descriptor->AssetName;
				}
				ImGui::PushID(static_cast<int>(i));
				char position[64];
				if (space.IsTwoD()) std::snprintf(position, sizeof(position), "(%.2f, %.2f)", sample.Position.x, sample.Position.y);
				else                std::snprintf(position, sizeof(position), "%.2f", sample.Position.x);
				if (ImGui::Selectable((label + "  " + position).CStr(), viewport->SelectedSample == static_cast<Int64>(i)))
					viewport->SelectedSample = static_cast<Int64>(i);
				ImGui::PopID();
			}
			if (ImGui::Button(ICON_FA_PLUS " Add Sample")) {
				BlendSample sample;
				sample.Position = space.ClampInput(viewport->PreviewInput);
				space.Samples.PushBack(sample);
				viewport->SelectedSample = static_cast<Int64>(space.Samples.Size()) - 1;
				changed = true;
			}
		}

		const Int64 selected = viewport->SelectedSample;
		if (selected >= 0 && selected < static_cast<Int64>(space.Samples.Size())
			&& ImGui::CollapsingHeader("Selected Sample", ImGuiTreeNodeFlags_DefaultOpen)) {
			BlendSample& sample = space.Samples[selected];
			changed |= TypeSerializer<TUsePointer<Animation>>::EditorControl(&sample.AnimationAsset, "Animation");
			if (space.IsTwoD()) changed |= ImGui::DragFloat2("Position", &sample.Position.x, 0.01f);
			else                changed |= ImGui::DragFloat("Position", &sample.Position.x, 0.01f);
			changed |= ImGui::Checkbox("Exclude From Sync", &sample.ExcludeFromSync);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Play on the graph clock instead of the shared, length-normalized phase.\n"
				                  "For clips without a step cycle (idle).");
			if (sample.AnimationAsset)
				ImGui::TextDisabled("%.2f s  (%d frames @ %.1f fps)", sample.AnimationAsset->GetDurationSeconds(),
				                    sample.AnimationAsset->FramesAmount, sample.AnimationAsset->FramesPerSecond);
			if (ImGui::Button(ICON_FA_TRASH " Delete Sample")) {
				space.Samples.RemoveAt(static_cast<UInt64>(selected));
				viewport->SelectedSample = -1;
				changed = true;
			}
		}

		if (changed) {
			space.RebuildTriangulation();
			PanelChangedAsset();
		}
	}
	EndPanel();
}
