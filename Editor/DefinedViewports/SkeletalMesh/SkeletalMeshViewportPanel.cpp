//
// Created by Plutex on 7/7/26.
//

#include "SkeletalMeshViewportPanel.h"
#include "PluEngine/FrameDemand.h"

#include <functional>
#include <glad/glad.h>
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "ImGuizmo/ImGuizmo.h"
#include "EditorAppContext.h"
#include "SkeletalMeshViewport.h"
#include "DefinedViewports/Skeleton/SkeletonHierarchyPanel.h"
#include "Managers/Scene/EditorCamera.h"
#include "PluEngine/Application.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Core/BoundingBox.h"
#include "PluEngine/AssetTypes/Animation/SkeletalAnimation.h"
#include "PluEngine/AssetTypes/Material/Material.h"
#include "PluEngine/AssetTypes/SkeletalMesh/SkeletalMesh.h"
#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"
#include "PluEngine/Gameplay/Components/SkeletalMeshComponent.h"
#include "PluEngine/Render/RenderingManager.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Render/GLFrameBuffer.h"
#include "PluEngine/Render/Renderer.h"
#include "PluEngine/Gameplay/RenderSnapshotBuilder.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "UI/IconsFontAwesome7.h"
#include "Utils/AttachPointOverlay.h"

extern Plu::ApplicationInfo* gApplicationInfo;
extern Plu::EditorAppContext* gEditorAppContext;

Plu::String Plu::SkeletalMeshViewportPanel::GetPanelName()
{
	return "Viewport";
}

void Plu::SkeletalMeshViewportPanel::OnClosed()
{
}

void Plu::SkeletalMeshViewportPanel::OnOpened()
{
	PLU_INFO("Skeletal Mesh Viewport Opened!");
	gEditorAppContext->EditorScenesManager->CreateOverlayScene();
	TUsePointer<SceneWorld> overlay = gEditorAppContext->EditorScenesManager->GetCurrentWorld();
	TUsePointer<EditorSkeletalMeshObject> meshObject = overlay->SpawnGameObject(EditorSkeletalMeshObject::GetStaticClass());
	TUsePointer<SkeletalMesh> skeletalMesh = gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
	meshObject->MeshComponent->SetSkeletalMesh(skeletalMesh);
	TUsePointer<SkeletalMeshViewport> parentViewport = DynamicCast<SkeletalMeshViewport>(GetParentViewport());
	meshObject->MeshComponent->SetMaterial(parentViewport->Material);
	// No re-fit here: the viewport remembers its own camera and gets it restored on re-open, so
	// framing only runs once (NeedsFraming's initial value) or when the user asks for it.
}

namespace
{
	// What a DrawBoneOverlay pass found: what the user clicked (if anything), plus the matrices a
	// gizmo needs for whatever is currently selected.
	struct BoneOverlayResult
	{
		// Bone/node joint or attach point marker clicked this frame; empty when nothing was hit.
		// At most one of the two is ever set — a click picks a single thing.
		Plu::String PickedNodeName;
		Plu::String PickedAttachPointName;

		// Selected bone: its current world matrix (gizmo target), the parent frame its local matrix
		// lives in (componentWorld * parentGlobal), and its base local matrix (bind/animated, before
		// any override) — enough to fold a gizmo edit back into a parent-space override.
		Matrix4 SelectedBoneWorld{1.0f};
		Matrix4 SelectedBoneParentFrame{1.0f};
		Matrix4 SelectedBoneBaseLocal{1.0f};

		// Selected attach point: the world frame of the node it hangs off, so a gizmo edit divides
		// back into the parent-relative location/rotation the asset stores. False when its parent
		// node isn't in this skeleton (a rename/reimport left the point orphaned).
		Matrix4 SelectedAttachPointParentFrame{1.0f};
		bool HasSelectedAttachPoint = false;
	};

	// Draws the animated bone skeleton, and the skeleton's attach points, over the rendered image.
	// Recomputes the same pose the render path builds (RenderSnapshotBuilder): per-node local =
	// animated TRS if the current animation has a track for it, else the bind-pose LocalMatrix;
	// global = parent * local. Attach points ride their parent node's animated global matrix, so
	// they follow the pose exactly as they will at runtime.
	//
	// Projection uses the *exact* view/projection the last frame was rendered with
	// (RenderSnapshotBuilder::GetLastFrame*Matrix), not a freshly rebuilt one. That guarantees
	// the joints land on the skinned mesh pixels regardless of FBO/window aspect: the displayed
	// texture and these matrices come from the same frame, and NDC maps linearly onto the whole
	// image rectangle. Everything is clipped to that rectangle so nothing bleeds outside.
	// Highlights the selected bone / attach point and, when `allowPick` is set, reports whichever
	// one is clicked this frame so the caller can update the shared selection.
	BoneOverlayResult DrawBoneOverlay(Plu::SkeletalMeshComponent* component, ImVec2 imagePos, ImVec2 imageSize,
	                                  const Plu::String& selectedName, const Plu::String& selectedAttachPointName,
	                                  bool allowPick)
	{
		using namespace Plu;
		BoneOverlayResult result;
		if (!component || !component->GetSkeletalMesh() || !component->GetSkeletalMesh()->MeshSkeleton) return result;
		Skeleton* skeleton = component->GetSkeletalMesh()->MeshSkeleton.GetRaw();
		SkeletonNode* root = skeleton->RootNode.GetRaw();
		if (!root) return result;

		const Matrix4 viewProj =
			RenderSnapshotBuilder::GetLastFrameProjectionMatrix() * RenderSnapshotBuilder::GetLastFrameViewMatrix();
		const Matrix4 componentWorld = component->GetRenderMatrix();

		auto project = [&](const Vec3& world, ImVec2& out) -> bool {
			const Vec4 clip = viewProj * Vec4(world, 1.0f);
			if (clip.w <= 0.0001f) return false; // behind camera
			const Vec3 ndc = Vec3(clip) / clip.w;
			out.x = imagePos.x + (ndc.x * 0.5f + 0.5f) * imageSize.x;
			out.y = imagePos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * imageSize.y;
			return true;
		};

		TUsePointer<Animation> animation = component->AnimationToShow;
		double animTimeTicks = 0.0;
		if (animation) {
			animTimeTicks = component->IsPlaying
				? glm::clamp(static_cast<double>(component->AnimationTimeTicks), 0.0, static_cast<double>(animation->FramesAmount))
				: static_cast<double>(glm::clamp(component->AnimationFrameToShow, 0, animation->FramesAmount));
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 boneCol = IM_COL32(240, 216, 100, 255);
		const ImU32 nodeCol = IM_COL32(140, 140, 150, 200);
		const ImU32 selCol  = IM_COL32(90, 200, 255, 255);

		// Closest pickable element under the cursor (for click-to-select). A node can be grabbed
		// by its joint (a bit generous) or by the bone line running to it from its parent (tighter,
		// since lines are thin); whichever is nearer wins. Squared distances in pixels.
		const ImVec2 mouse = ImGui::GetMousePos();
		constexpr float kJointPickSq = 10.0f * 10.0f;
		constexpr float kLinePickSq  = 6.0f * 6.0f;
		float bestPickSq = kJointPickSq;
		String pickedName;

		// Attach points are collected during the walk and drawn after it: their markers belong on
		// top of the bones, and their size is derived from the posed skeleton's on-screen extent,
		// which is only known once every joint has been visited. Each is kept with its parent
		// node's world frame — what it's authored against, and what a gizmo edit divides back out.
		struct PendingAttachPoint { String Name; Matrix4 ParentWorld; };
		DynamicArray<PendingAttachPoint> attachPoints;
		Vec3 poseMin(1e9f), poseMax(-1e9f);

		// Perpendicular (clamped to the endpoints) squared distance from p to segment a→b.
		auto distToSegmentSq = [](ImVec2 p, ImVec2 a, ImVec2 b) -> float {
			const float abx = b.x - a.x, aby = b.y - a.y;
			const float denom = abx * abx + aby * aby;
			float t = denom > 0.0f ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / denom : 0.0f;
			t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
			const float dx = p.x - (a.x + t * abx), dy = p.y - (a.y + t * aby);
			return dx * dx + dy * dy;
		};

		// Confine every stroke to the displayed image so bones can't spill over the panel chrome.
		drawList->PushClipRect(imagePos, ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y), true);

		// A bone's visual body is the segment(s) from its joint to its children, so the segment
		// parentNode→node belongs to parentNode: it's tinted when parentNode is selected, and
		// clicking it selects parentNode. The joint dot is the head of `node`, so it selects node.
		std::function<void(SkeletonNode*, SkeletonNode*, const Matrix4&, bool, ImVec2)> draw =
			[&](SkeletonNode* node, SkeletonNode* parentNode, const Matrix4& parentGlobal, bool hasParent, ImVec2 parentScreen) {
				if (!node) return;

				Matrix4 baseLocal = node->LocalMatrix;
				if (animation) {
					if (const AnimationTrack* track = animation->Tracks.Find(node->NodeName)) {
						const Vec3 loc = track->GetLocationAtTime(animTimeTicks);
						const Quaternion rot = track->GetRotationAtTime(animTimeTicks);
						const Vec3 scale = track->GetScaleAtTime(animTimeTicks);
						baseLocal = glm::translate(Matrix4(1.0f), loc) *
							glm::mat4_cast(rot) *
							glm::scale(Matrix4(1.0f), scale);
					}
				}

				// Temporary posing override (must match RenderSnapshotBuilder so overlay == mesh).
				Matrix4 local = baseLocal;
				if (!component->BoneLocalOverrides.IsEmpty()) {
					if (const Matrix4* ov = component->BoneLocalOverrides.Find(node->NodeName))
						local = (*ov) * baseLocal;
				}

				const Matrix4 global = parentGlobal * local;
				const Matrix4 nodeWorld = componentWorld * global;
				const Vec3 worldPos = Vec3(nodeWorld[3]);
				const bool isBone = dynamic_cast<SkeletonBone*>(node) != nullptr;
				const bool selected = !node->NodeName.IsEmpty() && node->NodeName == selectedName;
				const bool parentSelected = parentNode && !parentNode->NodeName.IsEmpty()
					&& parentNode->NodeName == selectedName;

				// Hand the caller what the gizmo needs for this bone (see BoneOverlayResult).
				if (selected) {
					result.SelectedBoneWorld = nodeWorld;
					result.SelectedBoneParentFrame = componentWorld * parentGlobal;
					result.SelectedBoneBaseLocal = baseLocal;
				}

				// Attach points hang off their parent node's posed frame, drawn regardless of
				// whether that node itself is a bone.
				for (const auto& [attachName, attachPoint] : skeleton->AttachPoints) {
					if (!attachPoint || attachPoint->ParentNodeName != node->NodeName) continue;
					attachPoints.PushBack(PendingAttachPoint{attachName, nodeWorld});
					if (attachName == selectedAttachPointName) {
						result.SelectedAttachPointParentFrame = nodeWorld;
						result.HasSelectedAttachPoint = true;
					}
				}
				poseMin = glm::min(poseMin, worldPos);
				poseMax = glm::max(poseMax, worldPos);

				bool nextHasParent = hasParent;
				ImVec2 nextParent = parentScreen;
				SkeletonNode* nextParentNode = parentNode;

				ImVec2 screen;
				if (project(worldPos, screen)) {
					// Segment parentNode→node: tinted when parentNode (the bone it represents) is selected.
					if (hasParent) {
						const ImU32 lineCol = parentSelected ? selCol : (isBone ? boneCol : nodeCol);
						const float lineWidth = parentSelected ? (isBone ? 3.5f : 2.5f) : (isBone ? 2.0f : 1.0f);
						drawList->AddLine(parentScreen, screen, lineCol, lineWidth);
					}
					drawList->AddCircleFilled(screen, isBone ? 3.5f : 2.5f, isBone ? boneCol : nodeCol);
					nextHasParent = true;
					nextParent = screen;
					nextParentNode = node;

					if (allowPick) {
						// Joint hit → select this node (the joint is its head).
						if (!node->NodeName.IsEmpty()) {
							const float dx = mouse.x - screen.x, dy = mouse.y - screen.y;
							const float d2 = dx * dx + dy * dy;
							if (d2 < bestPickSq) { bestPickSq = d2; pickedName = node->NodeName; }
						}
						// Line hit → select parentNode (the bone that segment belongs to).
						if (hasParent && parentNode && !parentNode->NodeName.IsEmpty()) {
							const float ld2 = distToSegmentSq(mouse, parentScreen, screen);
							if (ld2 <= kLinePickSq && ld2 < bestPickSq) { bestPickSq = ld2; pickedName = parentNode->NodeName; }
						}
					}
				} else {
					nextHasParent = false;
				}

				for (const auto& child : node->Children)
					draw(child.GetRaw(), nextParentNode, global, nextHasParent, nextParent);
			};

		draw(root, nullptr, Matrix4(1.0f), false, ImVec2());

		// Attach point markers, on top of the bones. Their axis stubs are sized off the posed
		// skeleton's own extent so they read the same on a rig authored in metres or centimetres.
		const float poseRadius = attachPoints.IsEmpty() ? 0.0f
			: glm::max(glm::length(poseMax - poseMin) * 0.5f, 1e-4f);
		float bestAttachPickSq = kJointPickSq;
		String pickedAttachPointName;
		for (const auto& pending : attachPoints)
		{
			const TOwningPointer<SkeletonAttachPoint>* found = skeleton->AttachPoints.Find(pending.Name);
			if (!found || !*found) continue;
			const float d2 = DrawAttachPointMarker(drawList, pending.ParentWorld * (*found)->GetLocalMatrix(),
			                                       poseRadius * 0.06f, pending.Name == selectedAttachPointName,
			                                       project, mouse);
			if (allowPick && d2 < bestAttachPickSq) {
				bestAttachPickSq = d2;
				pickedAttachPointName = pending.Name;
			}
		}

		drawList->PopClipRect();

		// Commit the pick only on a left click over the image, so hovering alone never reselects.
		// Attach points win ties: they're drawn on top, so that's what the user aimed at.
		if (allowPick && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			if (!pickedAttachPointName.IsEmpty())
				result.PickedAttachPointName = pickedAttachPointName;
			else
				result.PickedNodeName = pickedName;
		}
		return result;
	}

	// Keeps the overlay scene's attach point preview meshes in step with the viewport's preview
	// settings and the current pose: spawns/despawns the objects the settings ask for, and parks
	// each one on its attach point's posed world frame for this frame.
	//
	// The frame comes from SkeletalMeshComponent::TryGetAttachPointWorldMatrix, i.e. the same
	// bone transforms the renderer posed the mesh with last frame — so a preview follows animation
	// and live bone posing without this code re-walking the skeleton. It returns false until the
	// first snapshot has been built, in which case the object simply keeps its previous transform.
	void SyncAttachPointPreviews(Plu::SkeletalMeshViewport* viewport, Plu::SkeletalMeshComponent* component,
	                             Plu::TUsePointer<Plu::SceneWorld> overlay)
	{
		using namespace Plu;
		if (!viewport || !overlay) return;
		if (viewport->AttachPointPreviews.IsEmpty()) return;

		Skeleton* skeleton = (component && component->GetSkeletalMesh())
			? component->GetSkeletalMesh()->MeshSkeleton.GetRaw() : nullptr;

		// Attach points that no longer exist (deleted, or the skeleton was swapped out) take their
		// preview with them. Collected first: the map is being iterated.
		DynamicArray<String> orphaned;

		for (auto& [attachPointName, preview] : viewport->AttachPointPreviews)
		{
			const bool attachPointExists = skeleton && skeleton->AttachPoints.Contains(attachPointName);
			if (!attachPointExists) orphaned.PushBack(attachPointName);

			// No mesh picked (or nothing to hang it on) → drop the object, keep the settings.
			if (!attachPointExists || !preview.Mesh)
			{
				if (preview.Object)
					overlay->DeleteGameObject(*preview.Object->GetEngineObjectHandle());
				preview.Object = nullptr;
				continue;
			}

			if (!preview.Object)
				preview.Object = DynamicCast<EditorAttachPointPreviewObject>(
					overlay->SpawnGameObject(EditorAttachPointPreviewObject::GetStaticClass()));
			if (!preview.Object || !preview.Object->MeshComponent) continue;

			StaticMeshComponent* meshComponent = preview.Object->MeshComponent.GetRaw();
			if (meshComponent->GetStaticMesh() != preview.Mesh)
				meshComponent->SetStaticMesh(preview.Mesh);
			if (meshComponent->GetMaterial() != viewport->AttachPointPreviewMaterial)
				meshComponent->SetMaterial(viewport->AttachPointPreviewMaterial);

			Matrix4 attachPointWorld;
			if (component->TryGetAttachPointWorldMatrix(attachPointName, attachPointWorld))
			{
				preview.Object->SetObjectLocation(GetLocationFromMatrix(attachPointWorld));
				preview.Object->SetObjectRotation(GetRotationFromMatrix(attachPointWorld));
			}
			// Scale is the user's alone — the attach point frame carries none.
			preview.Object->SetObjectScale(preview.Scale);
		}

		for (const auto& attachPointName : orphaned)
			viewport->AttachPointPreviews.Remove(attachPointName);
	}

	// Fits the shared editor camera to the mesh's bind-pose bounds (same helper the scene's
	// "Fit In View" uses). Returns false while the mesh has no vertices yet, so the caller can
	// keep the framing request pending across async loads.
	bool FrameCameraToMesh(Plu::EditorSkeletalMeshObject* meshObject, ImVec2 imageSize)
	{
		using namespace Plu;
		if (!meshObject || !meshObject->MeshComponent) return false;
		SkeletalMeshComponent* component = meshObject->MeshComponent.GetRaw();
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
		const Vec3 newLoc = box.FitCamera(meshObject->GetObjectLocation(), camera->GetCameraRotation(),
		                                  Vec2(imageSize.x, imageSize.y), camera->GetCameraOptions()->FieldOfView);
		camera->SetCameraLocation(newLoc);
		return true;
	}
}

void Plu::SkeletalMeshViewportPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<SkeletalMeshViewport> parentViewport = DynamicCast<SkeletalMeshViewport>(GetParentViewport());
		TUsePointer<SceneWorld> overlay = gEditorAppContext->EditorScenesManager->GetCurrentWorld();
		TUsePointer<EditorSkeletalMeshObject> meshObject = overlay
			? DynamicCast<EditorSkeletalMeshObject>(overlay->GetGameObjectOfClass(EditorSkeletalMeshObject::GetStaticClass()))
			: nullptr;

		// The overlay scene doesn't tick outside PIE, so advance animation playback ourselves.
		// SkeletalMeshComponent::OnUpdate no-ops when not playing, so calling it every frame is safe.
		if (meshObject && meshObject->MeshComponent) {
			meshObject->MeshComponent->OnUpdate(deltaTime);
			// Playback advances without input; under power saving it has to ask for its frames.
			if (meshObject->MeshComponent->IsPlaying) RequestContinuousRedraw();
		}

		// Before the image is submitted, so the previews ride this frame's pose (the snapshot for
		// this frame is built after the panels have run).
		SyncAttachPointPreviews(parentViewport.GetRaw(),
		                        meshObject ? meshObject->MeshComponent.GetRaw() : nullptr, overlay);

		FrameBuffer* renderFBO = gApplicationInfo->AppRenderingManager->RequestMainFrameBuffer().GetRaw();

		ImVec2 viewportSize = ImGui::GetContentRegionAvail();
		float availW = viewportSize.x;
		float availH = viewportSize.y;

		float texAspect = (float)renderFBO->GetWidth() / (float)renderFBO->GetHeight();
		float availAspect = availW / availH;

		ImVec2 imageSize;
		if (availAspect > texAspect) {
			imageSize.y = availH;
			imageSize.x = imageSize.y * texAspect;
		} else {
			imageSize.x = availW;
			imageSize.y = imageSize.x / texAspect;
		}

		GLuint texID = renderFBO->GetColorTexture()->GetID();
		ImTextureID imguiTex = (ImTextureID)(intptr_t)texID;

		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::Image(imguiTex, imageSize, ImVec2(0,1), ImVec2(1,0));

		bool hovered = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + imageSize.x, pos.y + imageSize.y));

		// Fit the camera to the mesh on request; keep it pending until the mesh has vertices.
		if (parentViewport && parentViewport->NeedsFraming && FrameCameraToMesh(meshObject.GetRaw(), imageSize))
			parentViewport->NeedsFraming = false;

		// Bone overlay is projected into the exact image rectangle with the same aspect the FBO
		// rendered at, so joints line up with the skinned mesh underneath. It highlights the bone
		// selected in the hierarchy panel and lets the user click a joint/line to (re)select it; a
		// gizmo on the selected bone (translate/rotate/scale, world/local) poses it temporarily.
		if (parentViewport && parentViewport->ShowBones && meshObject && meshObject->MeshComponent) {
			SkeletalMeshComponent* component = meshObject->MeshComponent.GetRaw();
			SkeletonHierarchyPanel* hierarchy = GetParentViewport()->GetPanelSlow<SkeletonHierarchyPanel>();
			const String selectedName = hierarchy ? hierarchy->SelectedNodeName : String();

			const String selectedAttachPointName = hierarchy ? hierarchy->SelectedAttachPointName : String();

			// Don't let a click on the gizmo fall through to bone/attach point (re)selection.
			const bool allowPick = hovered && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
			const BoneOverlayResult overlay =
				DrawBoneOverlay(component, pos, imageSize, selectedName, selectedAttachPointName, allowPick);
			if (hierarchy && !overlay.PickedNodeName.IsEmpty()) {
				hierarchy->SelectedNodeName = overlay.PickedNodeName;
				hierarchy->SelectedAttachPointName.Clear();
			}
			if (hierarchy && !overlay.PickedAttachPointName.IsEmpty()) {
				hierarchy->SelectedAttachPointName = overlay.PickedAttachPointName;
				hierarchy->SelectedNodeName.Clear();
			}

			// One gizmo, one target: the hierarchy keeps bone and attach point selection mutually
			// exclusive, so whichever is set gets the handles.
			Matrix4 view = RenderSnapshotBuilder::GetLastFrameViewMatrix();
			Matrix4 proj = RenderSnapshotBuilder::GetLastFrameProjectionMatrix();
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
			ImGuizmo::SetRect(pos.x, pos.y, imageSize.x, imageSize.y);

			// Attach point gizmo: unlike bone posing this edits persistent asset data, writing the
			// manipulated world matrix back as a parent-relative location/rotation (so it stays glued
			// to the bone through any animation). It dirties the *skeleton*, which is where attach
			// points live — see MarkSkeletonAssetDirty.
			TOwningPointer<SkeletonAttachPoint>* selectedAttachPoint = nullptr;
			if (overlay.HasSelectedAttachPoint && component->GetSkeletalMesh()
				&& component->GetSkeletalMesh()->MeshSkeleton)
				selectedAttachPoint =
					component->GetSkeletalMesh()->MeshSkeleton->AttachPoints.Find(selectedAttachPointName);

			if (selectedAttachPoint && *selectedAttachPoint) {
				Matrix4 model = overlay.SelectedAttachPointParentFrame * (*selectedAttachPoint)->GetLocalMatrix();
				// Scale is deliberately not offered: an attach point has no scale to store.
				const GizmoOperation op = parentViewport->GizmoOp == GizmoOperation::ROTATE
					? GizmoOperation::ROTATE : GizmoOperation::TRANSLATE;
				if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
				                         PluGizmoOperationToImGuizmoOperation(op),
				                         PluGizmoModeToImGuizmoMode(parentViewport->GizmoSpace),
				                         glm::value_ptr(model))
					&& ImGuizmo::IsUsing()) {
					const Matrix4 newLocal = glm::inverse(overlay.SelectedAttachPointParentFrame) * model;
					(*selectedAttachPoint)->RelativeLocation = GetLocationFromMatrix(newLocal);
					(*selectedAttachPoint)->RelativeRotation = GetRotationFromMatrix(newLocal);
					MarkSkeletonAssetDirty(gApplicationInfo->AppAssetManager,
					                       component->GetSkeletalMesh()->MeshSkeleton.GetRaw());
				}
			}
			// Gizmo on the selected bone. The edited world matrix is converted back to a parent-space
			// override (override = newLocal * inverse(baseLocal)) — the same delta RenderSnapshotBuilder
			// applies — so the skinned mesh follows. Temporary only: BoneLocalOverrides is never saved.
			else if (hierarchy && !hierarchy->SelectedNodeName.IsEmpty()) {
				Matrix4 model = overlay.SelectedBoneWorld;

				if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
				                         PluGizmoOperationToImGuizmoOperation(parentViewport->GizmoOp),
				                         PluGizmoModeToImGuizmoMode(parentViewport->GizmoSpace),
				                         glm::value_ptr(model))
					&& ImGuizmo::IsUsing()) {
					const Matrix4 newLocal = glm::inverse(overlay.SelectedBoneParentFrame) * model;
					const Matrix4 newOverride = newLocal * glm::inverse(overlay.SelectedBoneBaseLocal);

					Matrix4* slot = component->BoneLocalOverrides.Find(hierarchy->SelectedNodeName);
					if (!slot) {
						component->BoneLocalOverrides.Insert(hierarchy->SelectedNodeName, Matrix4(1.0f));
						slot = component->BoneLocalOverrides.Find(hierarchy->SelectedNodeName);
					}
					*slot = newOverride;
				}
			}

			// Overlay controls pinned top-left over the image (like the Skeleton/Animation viewports):
			// gizmo operation (move/rotate/scale) and space (world/local).
			auto toolButton = [](const char* label, bool active) -> bool {
				if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				const bool clicked = ImGui::Button(label);
				if (active) ImGui::PopStyleColor();
				return clicked;
			};
			ImGui::SetCursorScreenPos(ImVec2(pos.x + 8.0f, pos.y + 8.0f));
			ImGui::BeginGroup();
			if (toolButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, parentViewport->GizmoOp == GizmoOperation::TRANSLATE))
				parentViewport->GizmoOp = GizmoOperation::TRANSLATE;
			ImGui::SameLine();
			if (toolButton(ICON_FA_ROTATE, parentViewport->GizmoOp == GizmoOperation::ROTATE))
				parentViewport->GizmoOp = GizmoOperation::ROTATE;
			ImGui::SameLine();
			if (toolButton(ICON_FA_MAXIMIZE, parentViewport->GizmoOp == GizmoOperation::SCALE))
				parentViewport->GizmoOp = GizmoOperation::SCALE;
			ImGui::SameLine(0.0f, 16.0f);
			const bool isWorld = parentViewport->GizmoSpace == GizmoOperationSpace::WORLD;
			// Single toggle showing (and switching) the current space.
			if (toolButton(isWorld ? ICON_FA_GLOBE " World" : ICON_FA_CUBE " Local", false))
				parentViewport->GizmoSpace = isWorld ? GizmoOperationSpace::LOCAL : GizmoOperationSpace::WORLD;

			// Reset just the selected bone — only shown once it's actually been posed.
			if (hierarchy && !hierarchy->SelectedNodeName.IsEmpty()
				&& component->BoneLocalOverrides.Find(hierarchy->SelectedNodeName)) {
				ImGui::SameLine(0.0f, 16.0f);
				if (ImGui::Button(ICON_FA_ROTATE_LEFT " Reset Transform"))
					component->BoneLocalOverrides.Remove(hierarchy->SelectedNodeName);
			}
			ImGui::EndGroup();
		}

		// Kamera stoi w PIE: PIE kasuje overlay tego viewportu i renderuje grę do tego samego FBO,
		// więc nie ma tu czego oglądać ani czym ruszać. Patrz notatka o PIE w Editor/CLAUDE.md.
		if (hovered) {
			if (!gEditorAppContext->EditorScenesManager->IsInPIE()) {
				if (gEditorAppContext->EditorSceneCamera) {
					DynamicCast<EditorSceneCamera>(gEditorAppContext->EditorSceneCamera)->OnUpdate(deltaTime);
				}
			}
		}
	}
	EndPanel();
}
