//
// Created by Plutex on 7/7/26.
//

#include "AnimationViewport.h"
#include "PluEngine/FrameDemand.h"

#include <cmath>
#include "glm/common.hpp"
#include "AnimationPreviewPanel.h"
#include "AnimationTimelinePanel.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/Animation/SkeletalAnimation.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"

extern Plu::ApplicationInfo* gApplicationInfo;

void Plu::AnimationViewport::OnOpened()
{
	// Preview first so it docks into the larger (top) region; timeline underneath.
	AddPanel(AnimationPreviewPanel::GetStaticClass(), false);
	AddPanel(AnimationTimelinePanel::GetStaticClass(), false);
}

void Plu::AnimationViewport::OnClosed()
{
}

void Plu::AnimationViewport::OnPanelRegister()
{
	AnimationPreviewPanel* previewPanel = GetPanelSlow<AnimationPreviewPanel>();
	AnimationTimelinePanel* timelinePanel = GetPanelSlow<AnimationTimelinePanel>();
	if (previewPanel && timelinePanel) {
		ImGuiID dockspaceID = GetWindowDockID();

		ImGui::DockBuilderRemoveNode(dockspaceID);
		ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetWindowSize());

		// Preview on top (~60%), timeline docked below it.
		ImGuiID top, bottom;
		ImGui::DockBuilderSplitNode(dockspaceID, ImGuiDir_Down, 0.4f, &bottom, &top);

		ImGui::DockBuilderDockWindow(previewPanel->GetPanelTitle().CStr(), top);
		ImGui::DockBuilderDockWindow(timelinePanel->GetPanelTitle().CStr(), bottom);
		ImGui::DockBuilderFinish(dockspaceID);
	}
}

void Plu::AnimationViewport::ApplyTransportShortcuts(const TUsePointer<Animation>& animation)
{
	if (!animation) return;

	// Both panels may report focus while docked together; apply the shortcuts only once per frame.
	const int frame = ImGui::GetFrameCount();
	if (frame == mShortcutFrame) return;
	mShortcutFrame = frame;

	const double durationTicks = static_cast<double>(animation->FramesAmount);

	if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
		IsPlaying = !IsPlaying;
	if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
		PlaybackTimeTicks = 0.0;
	if (ImGui::IsKeyPressed(ImGuiKey_End, false))
		PlaybackTimeTicks = durationTicks;
	if (ImGui::IsKeyPressed(ImGuiKey_L, false))
		Loop = !Loop;

	// Arrows step a single tick and pause, so you can settle on an exact pose.
	if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
		IsPlaying = false;
		PlaybackTimeTicks = glm::clamp(PlaybackTimeTicks - 1.0, 0.0, durationTicks);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
		IsPlaying = false;
		PlaybackTimeTicks = glm::clamp(PlaybackTimeTicks + 1.0, 0.0, durationTicks);
	}
}

void Plu::AnimationViewport::OnUpdate(float deltaTime)
{
	if (BeginWindow()) {
		// Single point that advances the playhead each frame (before the panels read it), so time
		// can never double-advance regardless of which panels are visible.
		TUsePointer<Animation> animation = gApplicationInfo->AppAssetManager->GetAssetData(GetAssetDescriptor());
		if (animation && IsPlaying && animation->FramesAmount > 0 && animation->FramesPerSecond > 0.0f)
		{
			// Playback advances without input; under power saving it has to ask for its frames.
			RequestContinuousRedraw();
			const double durationTicks = static_cast<double>(animation->FramesAmount);
			PlaybackTimeTicks += static_cast<double>(deltaTime) *
				static_cast<double>(animation->FramesPerSecond) * static_cast<double>(PlaybackSpeed);

			if (PlaybackTimeTicks >= durationTicks)
			{
				if (Loop)
					PlaybackTimeTicks = std::fmod(PlaybackTimeTicks, durationTicks);
				else {
					PlaybackTimeTicks = durationTicks;
					IsPlaying = false;
				}
			}
			if (PlaybackTimeTicks < 0.0)
				PlaybackTimeTicks = 0.0;
		}

		UpdatePanels(deltaTime);
	}
	EndWindow();
}
