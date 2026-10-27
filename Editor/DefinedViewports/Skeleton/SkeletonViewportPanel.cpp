//
// Created by Plutex on 7/6/26.
//

#include "SkeletonViewportPanel.h"

#include <functional>
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "ImGuizmo/ImGuizmo.h"
#include "SkeletonViewport.h"
#include "SkeletonHierarchyPanel.h"
#include "Managers/Scene/EditorCamera.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Render/RenderingInterfaces.h"
#include "UI/IconsFontAwesome7.h"
#include "PluEngine/Core/Widgets/Buttons.h"
#include "Utils/AttachPointOverlay.h"
#include "Utils/GizmoUtils.h"

extern Plu::ApplicationInfo* gApplicationInfo;

Plu::String Plu::SkeletonViewportPanel::GetPanelName()
{
	return "Preview";
}

void Plu::SkeletonViewportPanel::OnClosed()
{
}

void Plu::SkeletonViewportPanel::OnOpened()
{
	// No re-framing here: the viewport's camera is saved and restored per viewport, so re-opening
	// puts the user back where they left off. Initial framing runs off NeedsFraming's default.
}

// Walks the whole tree accumulating world matrices, invoking visit(node, worldPos, isBone)
// for every node. Traversal ignores visibility — that only affects what gets drawn.
static void ForEachNode(Plu::SkeletonNode* node, const Matrix4& parentWorld,
                        const std::function<void(Plu::SkeletonNode*, const Vec3&, bool)>& visit)
{
	if (!node) return;
	const Matrix4 world = parentWorld * node->LocalMatrix;
	const Vec3 pos = Vec3(world[3]);
	const bool isBone = dynamic_cast<Plu::SkeletonBone*>(node) != nullptr;
	visit(node, pos, isBone);
	for (const auto& child : node->Children)
		ForEachNode(child.GetRaw(), world, visit);
}

void Plu::SkeletonViewportPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		SkeletonViewport* viewport = DynamicCast<SkeletonViewport>(GetParentViewport()).GetRaw();
		// Node visibility + selection are owned by the sibling hierarchy panel (shared, single
		// source of truth). Tiny map, so the "slow" lookup is negligible here.
		SkeletonHierarchyPanel* hierarchy = GetParentViewport()->GetPanelSlow<SkeletonHierarchyPanel>();
		TUsePointer<Skeleton> skeleton = gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());

		ImVec2 canvasPos = ImGui::GetCursorScreenPos();
		ImVec2 canvasSize = ImGui::GetContentRegionAvail();
		if (canvasSize.x < 50.0f) canvasSize.x = 50.0f;
		if (canvasSize.y < 50.0f) canvasSize.y = 50.0f;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
		                        IM_COL32(28, 28, 32, 255));

		// Reserve the draw region with a Dummy, *not* an InvisibleButton: a button is an
		// interactive item, so covering the canvas with one keeps ImGui::IsAnyItemHovered() true
		// the whole time the cursor is over the preview — and ImGuizmo::CanActivate() refuses to
		// grab a handle while any item is hovered/active, which would make the attach point gizmo
		// dead on arrival. Dummy claims no ID, so the gizmo (and the later overlay widgets) get
		// the mouse. Hover is a plain rect test; camera navigation reads the global input backend
		// (via EditorSceneCamera::OnUpdate) and never needed the button's capture anyway.
		ImGui::Dummy(canvasSize);
		const bool hovered = ImGui::IsWindowHovered() &&
			ImGui::IsMouseHoveringRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y));

		EditorSceneCamera* camera = viewport ? viewport->GetEditorCamera() : nullptr;
		if (viewport && camera && hierarchy && skeleton && skeleton->RootNode)
		{

			// Normalize display size: skeletons can be authored at wildly different scales, and
			// the camera fly speed is fixed, so a huge rig would feel unmovable. Scale every
			// position uniformly to a constant on-screen radius so navigation feels the same
			// regardless of source units.
			Vec3 mn(1e9f), mx(-1e9f);
			int count = 0;
			ForEachNode(skeleton->RootNode.GetRaw(), Matrix4(1.0f),
				[&](SkeletonNode*, const Vec3& p, bool) {
					mn = glm::min(mn, p); mx = glm::max(mx, p); ++count;
				});
			constexpr float kTargetRadius = 1.5f;
			const float rawRadius = count > 0 ? glm::max(glm::length(mx - mn) * 0.5f, 1e-4f) : 1.0f;
			const float displayScale = kTargetRadius / rawRadius;
			const Vec3 center = count > 0 ? (mn + mx) * 0.5f * displayScale : Vec3(0.0f);
			const Matrix4 rootMatrix = glm::scale(Matrix4(1.0f), Vec3(displayScale));

			// --- Auto-frame to the (now normalized) skeleton on request ---
			if (viewport->NeedsFraming)
			{
				if (count > 0)
				{
					// Move the camera back along its current forward so the skeleton lands in front
					// of it (location only, like the scene "Fit In View"). We deliberately don't set
					// rotation: GetLookAtRotatorDegrees uses a +Z-forward convention inverted vs the
					// renderer's -Z view, so feeding it here would point the camera away.
					const float fov = camera->GetCameraOptions()->FieldOfView;
					const float dist = glm::max(kTargetRadius / glm::tan(glm::radians(fov * 0.5f)) * 1.2f, 0.5f);
					const Vec3 fwd = GetForwardVector(camera->GetCameraRotation());
					camera->SetCameraLocation(center - fwd * dist);
				}
				viewport->NeedsFraming = false;
			}

			// --- Camera input: drive the shared editor camera exactly as the scene viewport does,
			// so controls and directions are identical (WASD fly, RMB look, middle pan, scroll
			// forward). Gated on hover so it only moves while the canvas is focused — hovering is
			// also what hands this viewport the camera, so it's this viewport's view being moved. ---
			if (hovered)
				camera->OnUpdate(deltaTime);

			// --- Build view/projection from the camera (same convention as RenderSnapshotBuilder) ---
			const Vec3 camLoc = camera->GetCameraLocation();
			const Vec3 camRot = camera->GetCameraRotation();
			const Matrix4 view = glm::inverse(
				glm::translate(Matrix4(1.0f), camLoc) *
				glm::mat4_cast(glm::quat(glm::radians(camRot))));
			const float aspect = canvasSize.x / canvasSize.y;
			const Matrix4 proj = glm::perspective(
				glm::radians(camera->GetCameraOptions()->FieldOfView), aspect, 0.01f, 10000.0f);
			const Matrix4 viewProj = proj * view;

			auto project = [&](const Vec3& world, ImVec2& out) -> bool {
				const Vec4 clip = viewProj * Vec4(world, 1.0f);
				if (clip.w <= 0.0001f) return false; // behind camera
				const Vec3 ndc = Vec3(clip) / clip.w;
				out.x = canvasPos.x + (ndc.x * 0.5f + 0.5f) * canvasSize.x;
				out.y = canvasPos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * canvasSize.y;
				return true;
			};

			// --- Draw the skeleton ---
			// parentPos/hasParent describe the nearest *drawn* ancestor so hidden nodes are
			// transparently skipped while still contributing their transform.
			const ImU32 boneCol   = IM_COL32(240, 216, 100, 255);
			const ImU32 nodeCol   = IM_COL32(140, 140, 150, 200);
			const ImU32 selCol    = IM_COL32(90, 200, 255, 255);

			// Attach points are collected during the walk and drawn afterwards, so their markers
			// sit on top of the bones instead of being overdrawn by later branches. Each one is
			// kept with its parent node's world matrix — that's the frame it's authored in, and
			// what the gizmo folds its edit back into.
			struct PendingAttachPoint { String Name; Matrix4 ParentWorld; };
			DynamicArray<PendingAttachPoint> attachPoints;

			std::function<void(SkeletonNode*, const Matrix4&, bool, ImVec2)> draw =
				[&](SkeletonNode* node, const Matrix4& parentWorld, bool hasParent, ImVec2 parentPos) {
					if (!node) return;
					const Matrix4 world = parentWorld * node->LocalMatrix;
					const Vec3 pos = Vec3(world[3]);
					const bool isBone = dynamic_cast<SkeletonBone*>(node) != nullptr;
					const bool visible = isBone || hierarchy->ShowNodes;
					const bool selected = !node->NodeName.IsEmpty() && node->NodeName == hierarchy->SelectedNodeName;

					bool nextHasParent = hasParent;
					ImVec2 nextParentPos = parentPos;

					if (visible)
					{
						ImVec2 screen;
						if (project(pos, screen))
						{
							if (hasParent)
								drawList->AddLine(parentPos, screen, isBone ? boneCol : nodeCol, isBone ? 2.0f : 1.0f);
							const ImU32 jointCol = selected ? selCol : (isBone ? boneCol : nodeCol);
							const float r = selected ? 5.0f : (isBone ? 3.5f : 2.5f);
							drawList->AddCircleFilled(screen, r, jointCol);
							if (selected)
								drawList->AddCircle(screen, r + 2.5f, selCol, 0, 1.5f);
							nextHasParent = true;
							nextParentPos = screen;
						}
						else
						{
							// Visible but behind camera: break the line chain across it.
							nextHasParent = false;
						}
					}

					// Attach points hang off their parent node whether or not it's drawn: they're
					// explicit user data, so ShowNodes must never hide them.
					for (const auto& [attachName, attachPoint] : skeleton->AttachPoints)
						if (attachPoint && attachPoint->ParentNodeName == node->NodeName)
							attachPoints.PushBack(PendingAttachPoint{attachName, world});

					for (const auto& child : node->Children)
						draw(child.GetRaw(), world, nextHasParent, nextParentPos);
				};

			draw(skeleton->RootNode.GetRaw(), rootMatrix, false, ImVec2());

			// --- Attach points: markers, click-to-select, and a gizmo on the selected one ---
			// Sizes are in the normalized display space (see kTargetRadius), so a socket reads the
			// same whether the rig was authored in metres or centimetres.
			const ImVec2 mouse = ImGui::GetMousePos();
			const bool allowPick = hovered && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
			constexpr float kAttachPickSq = 12.0f * 12.0f;
			float bestPickSq = kAttachPickSq;
			String pickedAttachPoint;
			Matrix4 selectedParentWorld(1.0f);
			bool haveSelectedAttachPoint = false;

			for (const auto& pending : attachPoints)
			{
				TOwningPointer<SkeletonAttachPoint>* found = skeleton->AttachPoints.Find(pending.Name);
				if (!found || !*found) continue;
				const bool selected = pending.Name == hierarchy->SelectedAttachPointName;
				if (selected)
				{
					selectedParentWorld = pending.ParentWorld;
					haveSelectedAttachPoint = true;
				}
				const float d2 = DrawAttachPointMarker(drawList, pending.ParentWorld * (*found)->GetLocalMatrix(),
				                                       kTargetRadius * 0.06f, selected, project, mouse);
				if (allowPick && d2 < bestPickSq)
				{
					bestPickSq = d2;
					pickedAttachPoint = pending.Name;
				}
			}

			// Commit the pick only on a click, so hovering alone never reselects.
			if (!pickedAttachPoint.IsEmpty() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				hierarchy->SelectedAttachPointName = pickedAttachPoint;
				hierarchy->SelectedNodeName.Clear();
			}

			// The gizmo manipulates the point's world matrix; dividing the parent node's frame back
			// out yields exactly the parent-relative location/rotation the asset stores (rootMatrix's
			// display scale cancels through the same inverse).
			if (haveSelectedAttachPoint)
			{
				TOwningPointer<SkeletonAttachPoint>* found =
					skeleton->AttachPoints.Find(hierarchy->SelectedAttachPointName);
				if (found && *found)
				{
					Matrix4 model = selectedParentWorld * (*found)->GetLocalMatrix();

					ImGuizmo::SetOrthographic(false);
					ImGuizmo::SetDrawlist(drawList);
					ImGuizmo::SetRect(canvasPos.x, canvasPos.y, canvasSize.x, canvasSize.y);

					// Scale is deliberately not offered: an attach point has no scale to store.
					const GizmoOperation op = viewport->GizmoOp == GizmoOperation::ROTATE
						? GizmoOperation::ROTATE : GizmoOperation::TRANSLATE;
					if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
					                         PluGizmoOperationToImGuizmoOperation(op),
					                         PluGizmoModeToImGuizmoMode(viewport->GizmoSpace),
					                         glm::value_ptr(model))
						&& ImGuizmo::IsUsing())
					{
						const Matrix4 newLocal = glm::inverse(selectedParentWorld) * model;
						(*found)->RelativeLocation = GetLocationFromMatrix(newLocal);
						(*found)->RelativeRotation = GetRotationFromMatrix(newLocal);
						MarkSkeletonAssetDirty(gApplicationInfo->AppAssetManager, skeleton.GetRaw());
					}
				}
			}
		}
		else
		{
			const char* msg = "No skeleton data";
			ImVec2 ts = ImGui::CalcTextSize(msg);
			drawList->AddText(ImVec2(canvasPos.x + (canvasSize.x - ts.x) * 0.5f,
			                         canvasPos.y + (canvasSize.y - ts.y) * 0.5f),
			                  IM_COL32(150, 150, 150, 255), msg);
		}

		// --- Overlay controls, pinned to the top-left corner ---
		if (viewport)
		{
			ImGui::SetCursorScreenPos(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 8.0f));
			ImGui::BeginGroup();
			if (hierarchy)
			{
				ImGui::Checkbox(ICON_FA_CIRCLE_NODES " Show Nodes", &hierarchy->ShowNodes);
				ImGui::SameLine();
			}
			if (ImGui::SmallButton(ICON_FA_CROSSHAIRS " Frame"))
				viewport->NeedsFraming = true;

			// Gizmo mode, shown only while there's an attach point for it to act on (nothing else
			// in this viewport is manipulable).
			if (hierarchy && !hierarchy->SelectedAttachPointName.IsEmpty())
			{
				ImGui::SameLine(0.0f, 16.0f);
				if (ImGuiWidgets::SmallToggleButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, viewport->GizmoOp == GizmoOperation::TRANSLATE, "Move"))
					viewport->GizmoOp = GizmoOperation::TRANSLATE;
				ImGui::SameLine();
				if (ImGuiWidgets::SmallToggleButton(ICON_FA_ROTATE, viewport->GizmoOp == GizmoOperation::ROTATE, "Rotate"))
					viewport->GizmoOp = GizmoOperation::ROTATE;
				ImGui::SameLine();
				const bool isWorld = viewport->GizmoSpace == GizmoOperationSpace::WORLD;
				// Single toggle showing (and switching) the current space.
				if (ImGui::SmallButton(isWorld ? ICON_FA_GLOBE " World" : ICON_FA_CUBE " Local"))
					viewport->GizmoSpace = isWorld ? GizmoOperationSpace::LOCAL : GizmoOperationSpace::WORLD;
			}
			ImGui::EndGroup();
		}
	}
	EndPanel();
}
