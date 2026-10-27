//
// Created by Plutex on 7/7/26.
//

#include "SkeletalMeshDetailsPanel.h"

#include "glm/gtc/type_ptr.hpp"
#include "EditorAppContext.h"
#include "SkeletalMeshViewport.h"
#include "DefinedViewports/Skeleton/SkeletonHierarchyPanel.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetTypes/Animation/SkeletalAnimation.h"
#include "PluEngine/AssetTypes/Material/Material.h"
#include "PluEngine/AssetTypes/SkeletalMesh/SkeletalMesh.h"
#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"
#include "PluEngine/Gameplay/Components/SkeletalMeshComponent.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "UI/IconsFontAwesome7.h"
#include "PluEngine/Core/Widgets/DragVector.h"

extern Plu::ApplicationInfo* gApplicationInfo;
extern Plu::EditorAppContext* gEditorAppContext;

Plu::String Plu::SkeletalMeshDetailsPanel::GetPanelName()
{
	return "Properties";
}

void Plu::SkeletalMeshDetailsPanel::OnClosed()
{
}

void Plu::SkeletalMeshDetailsPanel::OnOpened()
{
}

void Plu::SkeletalMeshDetailsPanel::OnUpdate(float deltaTime)
{
	if (!BeginPanel())
	{
		EndPanel();
		return;
	}

	TUsePointer<SkeletalMeshViewport> parentViewport = DynamicCast<SkeletalMeshViewport>(GetParentViewport());
	TUsePointer<SkeletalMesh> skeletalMesh = gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());

	TUsePointer<SceneWorld> overlay = gEditorAppContext->EditorScenesManager->GetCurrentWorld();
	TUsePointer<EditorSkeletalMeshObject> meshObject = overlay
		? DynamicCast<EditorSkeletalMeshObject>(overlay->GetGameObjectOfClass(EditorSkeletalMeshObject::GetStaticClass()))
		: nullptr;
	SkeletalMeshComponent* component = (meshObject && meshObject->MeshComponent) ? meshObject->MeshComponent.GetRaw() : nullptr;

	// --- Preview material (view-only: not part of the asset, so never marks it dirty) ---
	TUsePointer<MaterialInfo> before = parentViewport->Material;
	TypeSerializer<TUsePointer<MaterialInfo>>::EditorControl(&parentViewport->Material, "Material");
	if (before != parentViewport->Material && component)
		component->SetMaterial(parentViewport->Material);

	ImGui::Separator();

	// --- Mesh / skeleton info ---
	if (skeletalMesh)
	{
		ImGui::Text("Vertices: %lu", skeletalMesh->MeshData.Vertices.Size());
		ImGui::Text("Triangles: %lu", skeletalMesh->MeshData.Indices.Size() / 3);
		if (skeletalMesh->MeshSkeleton)
			ImGui::Text(ICON_FA_BONE " Skeleton: %s (%llu bones)",
				skeletalMesh->MeshSkeleton->SkeletonName.CStr(),
				(unsigned long long)skeletalMesh->MeshSkeleton->CountBones());
		else
			ImGui::TextDisabled(ICON_FA_BONE " No skeleton");
	}

	ImGui::Separator();

	// --- Overlay / panel toggles ---
	ImGui::Checkbox(ICON_FA_CIRCLE_NODES " Show Bones", &parentViewport->ShowBones);
	ImGui::SameLine();
	ImGui::Checkbox(ICON_FA_SITEMAP " Show Hierarchy", &parentViewport->ShowHierarchy);

	// Mirror the toggle into the shared hierarchy panel's visibility (single source of truth is
	// the viewport flag, so it persists while the panel is hidden). Same reusable tree as
	// SkeletonViewport — just embedded here on the left.
	if (SkeletonHierarchyPanel* hierarchyPanel = GetParentViewport()->GetPanelSlow<SkeletonHierarchyPanel>())
		hierarchyPanel->Visible = parentViewport->ShowHierarchy;

	// Re-fit the camera to the mesh bounds (also runs automatically when the viewport opens).
	if (ImGui::Button(ICON_FA_CROSSHAIRS " Frame"))
		parentViewport->NeedsFraming = true;

	// --- Temporary bone posing (Show Bones + move the selected bone's gizmo; never saved) ---
	if (parentViewport->ShowBones)
	{
		ImGui::TextDisabled("Select a bone, drag its gizmo to pose it (preview only).");
		if (component && !component->BoneLocalOverrides.IsEmpty())
		{
			ImGui::SameLine();
			if (ImGui::SmallButton(ICON_FA_ROTATE_LEFT " Reset Pose"))
				component->BoneLocalOverrides.Clear();
		}
	}

	ImGui::Separator();

	// --- Attach point preview meshes (view-only, same rules as the material above) ---
	// A prop per attach point, spawned into the overlay scene and glued to the posed bone by
	// SkeletalMeshViewportPanel's sync, so it follows animation and live posing. Nothing here is
	// written to the Skeleton asset — it's an eyeballing aid, not authored data.
	if (ImGui::CollapsingHeader(ICON_FA_LOCATION_DOT " Attach Point Previews"))
	{
		Skeleton* skeleton = skeletalMesh ? skeletalMesh->MeshSkeleton.GetRaw() : nullptr;
		if (!skeleton || skeleton->AttachPoints.IsEmpty())
		{
			ImGui::TextDisabled("Skeleton has no attach points.");
		}
		else
		{
			for (const auto& [attachPointName, attachPoint] : skeleton->AttachPoints)
			{
				if (!attachPoint) continue;
				ImGui::PushID(attachPointName.CStr());
				ImGui::TextDisabled("%s", attachPointName.CStr());

				// Creating the entry on sight is deliberate: an entry with no mesh costs nothing
				// (the sync spawns no object for it) and it keeps the scale the user dialled in
				// when they clear the mesh and pick another one.
				AttachPointPreview& preview = parentViewport->AttachPointPreviews[attachPointName];
				TypeSerializer<TUsePointer<StaticMesh>>::EditorControl(&preview.Mesh, "Mesh");

				if (preview.Mesh)
				{
					ImGuiWidgets::DragVector("Scale", glm::value_ptr(preview.Scale), 3, 0.01f);
					if (ImGui::SmallButton(ICON_FA_ROTATE_LEFT " Reset Scale"))
						preview.Scale = Vec3(1.0f);
					ImGui::SameLine();
					if (ImGui::SmallButton(ICON_FA_XMARK " Clear"))
						preview.Mesh = nullptr;
				}
				ImGui::PopID();
			}
		}
	}

	ImGui::Separator();

	// --- Animation picker (loops the chosen clip; no timeline) ---
	if (ImGui::CollapsingHeader(ICON_FA_PERSON_RUNNING " Animation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (!component)
		{
			ImGui::TextDisabled("Preview not ready.");
		}
		else
		{
			// Reuse the engine's standard asset picker for TUsePointer<Animation> (same control
			// the scene details panel draws for this property) — filtered, searchable, no bespoke
			// list to maintain. Auto-play the freshly picked clip from its start.
			if (TypeSerializer<TUsePointer<Animation>>::EditorControl(&component->AnimationToShow, "Animation"))
			{
				component->AnimationFrameToShow = 0;
				component->AnimationTimeTicks = 0.0f;
				component->IsPlaying = component->AnimationToShow.IsValid();
			}

			// Playback controls (no scrubber / timeline).
			if (component->AnimationToShow)
			{
				if (ImGui::Button(component->IsPlaying ? ICON_FA_PAUSE " Pause" : ICON_FA_PLAY " Play"))
					component->IsPlaying = !component->IsPlaying;
				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_STOP " Reset"))
				{
					component->IsPlaying = false;
					component->AnimationFrameToShow = 0;
					component->AnimationTimeTicks = 0.0f;
				}
				ImGui::SameLine();
				ImGui::Checkbox("Loop", &component->LoopAnimation);

				ImGui::Text("%d frames @ %.1f fps",
					component->AnimationToShow->FramesAmount, component->AnimationToShow->FramesPerSecond);
			}
		}
	}

	EndPanel();
}
