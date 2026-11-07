//
// Created by Plutex on 10/14/26.
//

#include "BlendSpacePreviewPanel.h"

#include <glad/glad.h>
#include "BlendSpaceViewport.h"
#include "EditorAppContext.h"
#include "Managers/Scene/EditorCamera.h"
#include "PluEngine/Application.h"
#include "PluEngine/FrameDemand.h"
#include "PluEngine/Core/BoundingBox.h"
#include "PluEngine/AssetTypes/Animation/AnimGraphInstance.h"
#include "PluEngine/AssetTypes/Animation/BlendSpace.h"
#include "PluEngine/AssetTypes/SkeletalMesh/SkeletalMesh.h"
#include "PluEngine/Gameplay/Components/SkeletalMeshComponent.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "PluEngine/Render/GLFrameBuffer.h"
#include "PluEngine/Render/RenderingManager.h"
#include "PluEngine/Render/Renderer.h"
#include "UI/IconsFontAwesome7.h"

extern Plu::ApplicationInfo* gApplicationInfo;
extern Plu::EditorAppContext* gEditorAppContext;

namespace
{
	// Same framing as SkeletalMeshViewportPanel: fit the shared editor camera to the bind-pose
	// vertex bounds. False while the mesh has nothing to measure yet (async load) — retry later.
	bool FrameCameraToPreviewMesh(Plu::EditorBlendSpacePreviewObject* object, ImVec2 imageSize)
	{
		using namespace Plu;
		if (!object || !object->MeshComponent) return false;
		SkeletalMeshComponent* component = object->MeshComponent.GetRaw();
		if (!component->GetSkeletalMesh()) return false;
		const SkeletalMeshData& meshData = component->GetSkeletalMesh()->MeshData;
		if (meshData.Vertices.IsEmpty()) return false;

		EditorSceneCamera* camera = gEditorAppContext->EditorSceneCamera.GetRaw();
		if (!camera) return false;

		DynamicArray<Vec3> points;
		points.Reserve(meshData.Vertices.Size());
		for (const auto& vertex : meshData.Vertices)
			points.PushBack(vertex.Position);

		BoundingBox box = CreateBoundingBox(points);
		const Vec3 newLoc = box.FitCamera(object->GetObjectLocation(), camera->GetCameraRotation(),
		                                  Vec2(imageSize.x, imageSize.y), camera->GetCameraOptions()->FieldOfView);
		camera->SetCameraLocation(newLoc);
		return true;
	}
}

Plu::String Plu::BlendSpacePreviewPanel::GetPanelName()
{
	return "Preview";
}

void Plu::BlendSpacePreviewPanel::OnClosed()
{
}

void Plu::BlendSpacePreviewPanel::OnOpened()
{
	gEditorAppContext->EditorScenesManager->CreateOverlayScene();
	TUsePointer<SceneWorld> overlay = gEditorAppContext->EditorScenesManager->GetCurrentWorld();
	if (!overlay) return;
	TUsePointer<EditorBlendSpacePreviewObject> object =
		overlay->SpawnGameObject(EditorBlendSpacePreviewObject::GetStaticClass());
	TUsePointer<BlendSpaceViewport> viewport = DynamicCast<BlendSpaceViewport>(GetParentViewport());
	if (!object || !object->MeshComponent || !viewport) return;

	// The mesh itself is synced from BlendSpace::PreviewMesh every update (it can change while open).
	object->MeshComponent->SetMaterial(viewport->Material);
	object->MeshComponent->SetAnimGraph(viewport->GetPreviewGraph());
	object->MeshComponent->LoopAnimation = true;
}

void Plu::BlendSpacePreviewPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<BlendSpaceViewport> viewport = DynamicCast<BlendSpaceViewport>(GetParentViewport());
		TUsePointer<BlendSpace> space = viewport ? viewport->GetBlendSpace() : nullptr;
		TUsePointer<SceneWorld> overlay = gEditorAppContext->EditorScenesManager->GetCurrentWorld();
		TUsePointer<EditorBlendSpacePreviewObject> object = overlay
			? DynamicCast<EditorBlendSpacePreviewObject>(overlay->GetGameObjectOfClass(EditorBlendSpacePreviewObject::GetStaticClass()))
			: nullptr;
		SkeletalMeshComponent* component = object && object->MeshComponent ? object->MeshComponent.GetRaw() : nullptr;

		if (viewport && space && component) {
			viewport->SyncPreviewGraph();

			// Follow the asset's preview mesh; a new mesh gets framed once it has loaded.
			if (component->GetSkeletalMesh().GetRaw() != space->PreviewMesh.GetRaw()) {
				component->SetSkeletalMesh(space->PreviewMesh);
				viewport->NeedsFraming = true;
			}

			// Cursor -> preview graph variables. Set bumps the value revision only on a real change,
			// so an idle cursor keeps the pose cache hitting while paused.
			if (AnimGraphInstance* instance = component->GetAnimGraphInstance()) {
				instance->SetFloat("X", viewport->PreviewInput.x);
				instance->SetFloat("Y", viewport->PreviewInput.y);
			}

			// Sample/axis edits change the pose without touching the pose cache key (graph time,
			// variable values) — drop the cache so a paused preview still shows them.
			if (space->GetRevision() != mSeenRevision) {
				mSeenRevision = space->GetRevision();
				component->CachedPoseValid = false;
			}

			// The overlay scene doesn't tick outside PIE — advance playback here (OnUpdate no-ops
			// while paused).
			component->IsPlaying = viewport->IsPlaying;
			component->OnUpdate(deltaTime);
			if (viewport->IsPlaying && space->PreviewMesh) RequestContinuousRedraw();
		}

		FrameBuffer* renderFBO = gApplicationInfo->AppRenderingManager->RequestMainFrameBuffer().GetRaw();

		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float texAspect = (float)renderFBO->GetWidth() / (float)renderFBO->GetHeight();
		ImVec2 imageSize;
		if (available.x / available.y > texAspect) {
			imageSize.y = available.y;
			imageSize.x = imageSize.y * texAspect;
		} else {
			imageSize.x = available.x;
			imageSize.y = imageSize.x / texAspect;
		}

		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::Image((ImTextureID)(intptr_t)renderFBO->GetColorTexture()->GetID(), imageSize, ImVec2(0, 1), ImVec2(1, 0));
		const bool hovered = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + imageSize.x, pos.y + imageSize.y));

		if (viewport && viewport->NeedsFraming && FrameCameraToPreviewMesh(object.GetRaw(), imageSize))
			viewport->NeedsFraming = false;

		// Overlay controls pinned top-left over the image (same placement as the mesh viewports).
		ImGui::SetCursorScreenPos(ImVec2(pos.x + 8.0f, pos.y + 8.0f));
		ImGui::BeginGroup();
		if (viewport) {
			if (ImGui::Button(viewport->IsPlaying ? ICON_FA_PAUSE " Pause" : ICON_FA_PLAY " Play"))
				viewport->IsPlaying = !viewport->IsPlaying;
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_EXPAND " Frame"))
				viewport->NeedsFraming = true;
		}
		if (space && !space->PreviewMesh)
			ImGui::TextDisabled("Pick a Preview Mesh in the details panel.");
		ImGui::EndGroup();

		// No camera control in PIE — see "Znany problem: PIE" in Editor/CLAUDE.md.
		if (hovered && !gEditorAppContext->EditorScenesManager->IsInPIE() && gEditorAppContext->EditorSceneCamera)
			DynamicCast<EditorSceneCamera>(gEditorAppContext->EditorSceneCamera)->OnUpdate(deltaTime);
	}
	EndPanel();
}
