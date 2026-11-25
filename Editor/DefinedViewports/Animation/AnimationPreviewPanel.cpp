//
// Created by Plutex on 7/7/26.
//

#include "AnimationPreviewPanel.h"

#include <functional>
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "AnimationViewport.h"
#include "Managers/Scene/EditorCamera.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/Animation/SkeletalAnimation.h"
#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/PluUtils.h"
#include "UI/IconsFontAwesome7.h"

extern Plu::ApplicationInfo* gApplicationInfo;

Plu::String Plu::AnimationPreviewPanel::GetPanelName()
{
	return "Preview";
}

void Plu::AnimationPreviewPanel::OnClosed()
{
}

void Plu::AnimationPreviewPanel::OnOpened()
{
	// No re-framing here: the viewport's camera is saved and restored per viewport, so re-opening
	// puts the user back where they left off. Initial framing runs off NeedsFraming's default.
}

void Plu::AnimationPreviewPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		AnimationViewport* viewport = DynamicCast<AnimationViewport>(GetParentViewport()).GetRaw();
		TUsePointer<Animation> animation = gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
		TUsePointer<Skeleton> skeleton = animation ? animation->AnimationSkeleton : nullptr;

		ImVec2 canvasPos = ImGui::GetCursorScreenPos();
		ImVec2 canvasSize = ImGui::GetContentRegionAvail();
		if (canvasSize.x < 50.0f) canvasSize.x = 50.0f;
		if (canvasSize.y < 50.0f) canvasSize.y = 50.0f;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
		                        IM_COL32(28, 28, 32, 255));

		// Full-canvas invisible button reserves the draw region and gives us a hover flag.
		// AllowOverlap lets the later-submitted overlay widgets steal hover/clicks over it.
		ImGui::SetNextItemAllowOverlap();
		ImGui::InvisibleButton("##animcanvas", canvasSize);
		const bool hovered = ImGui::IsItemHovered();

		// Transport shortcuts while the preview is focused (camera nav uses WASD/mouse only, so
		// Space/Home/End/L/arrows are free to drive playback).
		if (viewport && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
			viewport->ApplyTransportShortcuts(animation);

		EditorSceneCamera* camera = viewport ? viewport->GetEditorCamera() : nullptr;
		if (viewport && camera && animation && skeleton && skeleton->RootNode)
		{
			const double animTimeTicks = glm::clamp(viewport->PlaybackTimeTicks, 0.0, static_cast<double>(animation->FramesAmount));

			// Samples a node's animated local transform, falling back to its bind-pose LocalMatrix
			// when no track animates it (FBX pivot nodes, static bones).
			auto localOf = [&](Plu::SkeletonNode* node) -> Matrix4 {
				if (const AnimationTrack* track = animation->Tracks.Find(node->NodeName))
				{
					const Vec3 loc = track->GetLocationAtTime(animTimeTicks);
					const Quaternion rot = track->GetRotationAtTime(animTimeTicks);
					const Vec3 scale = track->GetScaleAtTime(animTimeTicks);
					return glm::translate(Matrix4(1.0f), loc) * glm::mat4_cast(rot) * glm::scale(Matrix4(1.0f), scale);
				}
				return node->LocalMatrix;
			};

			// Normalize display size: skeletons can be authored at wildly different scales while the
			// camera fly speed is fixed. Measure the animated pose bounds and scale every position to
			// a constant on-screen radius so navigation feels the same regardless of source units.
			Vec3 mn(1e9f), mx(-1e9f);
			int count = 0;
			std::function<void(SkeletonNode*, const Matrix4&)> measure =
				[&](SkeletonNode* node, const Matrix4& parentWorld) {
					if (!node) return;
					const Matrix4 world = parentWorld * localOf(node);
					const Vec3 p = Vec3(world[3]);
					mn = glm::min(mn, p); mx = glm::max(mx, p); ++count;
					for (const auto& child : node->Children)
						measure(child.GetRaw(), world);
				};
			measure(skeleton->RootNode.GetRaw(), Matrix4(1.0f));

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
					const float fov = camera->GetCameraOptions()->FieldOfView;
					const float dist = glm::max(kTargetRadius / glm::tan(glm::radians(fov * 0.5f)) * 1.2f, 0.5f);
					const Vec3 fwd = GetForwardVector(camera->GetCameraRotation());
					camera->SetCameraLocation(center - fwd * dist);
				}
				viewport->NeedsFraming = false;
			}

			// --- Camera input: drive the shared editor camera exactly as the scene viewport does,
			// gated on hover so it only moves while the canvas is focused — hovering is also what
			// hands this viewport the camera, so it's this viewport's view being moved. ---
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

			// Confine every stroke to the canvas so bones can't spill over the panel chrome.
			drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

			const ImU32 boneCol = IM_COL32(240, 216, 100, 255);
			const ImU32 nodeCol = IM_COL32(140, 140, 150, 200);
			const ImU32 selCol  = IM_COL32(90, 200, 255, 255);

			// parentPos/hasParent describe the nearest *drawn* ancestor so hidden nodes are
			// transparently skipped while still contributing their transform.
			std::function<void(SkeletonNode*, const Matrix4&, bool, ImVec2)> draw =
				[&](SkeletonNode* node, const Matrix4& parentWorld, bool hasParent, ImVec2 parentPos) {
					if (!node) return;
					const Matrix4 world = parentWorld * localOf(node);
					const Vec3 pos = Vec3(world[3]);
					const bool isBone = dynamic_cast<SkeletonBone*>(node) != nullptr;
					const bool visible = isBone || viewport->ShowNodes;
					const bool selected = viewport->HasSelection && !node->NodeName.IsEmpty() &&
						node->NodeName == viewport->SelectedTrack;

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
							nextHasParent = false; // visible but behind camera: break the line chain
						}
					}

					for (const auto& child : node->Children)
						draw(child.GetRaw(), world, nextHasParent, nextParentPos);
				};

			draw(skeleton->RootNode.GetRaw(), rootMatrix, false, ImVec2());
			drawList->PopClipRect();
		}
		else
		{
			const char* msg = animation ? "Animation has no skeleton" : "No animation data";
			ImVec2 ts = ImGui::CalcTextSize(msg);
			drawList->AddText(ImVec2(canvasPos.x + (canvasSize.x - ts.x) * 0.5f,
			                         canvasPos.y + (canvasSize.y - ts.y) * 0.5f),
			                  IM_COL32(150, 150, 150, 255), msg);
		}

		// --- View controls, pinned to the top-left corner. Playback (play/pause, stepping, loop,
		// speed, scrubbing) lives entirely in the timeline panel; this overlay is view-only. ---
		if (viewport)
		{
			ImGui::SetCursorScreenPos(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 8.0f));
			ImGui::BeginGroup();
			ImGui::Checkbox(ICON_FA_CIRCLE_NODES " Nodes", &viewport->ShowNodes);
			ImGui::SameLine();
			if (ImGui::SmallButton(ICON_FA_CROSSHAIRS " Frame"))
				viewport->NeedsFraming = true;
			ImGui::EndGroup();
		}
	}
	EndPanel();
}
