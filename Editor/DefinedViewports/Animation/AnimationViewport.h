//
// Created by Plutex on 7/7/26.
//

#ifndef PLUENGINE_ANIMATIONVIEWPORT_H
#define PLUENGINE_ANIMATIONVIEWPORT_H
#include "EditorViewports/IEditorViewport.h"
#include "AnimationViewport.generated.h"
#include "PluSTL/PluSTL_FWD.h"

namespace Plu
{
	struct Animation;

	// Viewport for standalone Animation assets. Two panels share the state below:
	//   - AnimationPreviewPanel : a live 3D preview of the animated skeleton (bones projected
	//     with ImGui draw lists — the animation references a Skeleton, not a mesh), scrubbed by
	//     the playhead below.
	//   - AnimationTimelinePanel: the keyframe timeline + transport (play/pause/loop/speed) and a
	//     draggable playhead that drives PlaybackTimeTicks.
	//
	// Everything is drawn with ImGui draw lists on the main thread (no GL / render snapshot),
	// same approach as SkeletonViewport. All fields here are pure preview state — they never
	// mark the asset dirty.
	PLU_CLASS()
	class AnimationViewport : public IEditorViewport
	{
		REFLECTION_BODY_ANIMATIONVIEWPORT()
	public:
		AnimationViewport() = default;
		virtual ~AnimationViewport() override = default;

		// --- Timeline view state ---
		// Horizontal zoom of the timeline, in on-screen pixels per second.
		float PixelsPerSecond = 200.0f;

		// Set true to recompute PixelsPerSecond so the whole clip fits the panel width on the
		// next update (on open, or when the user clicks "Fit").
		bool NeedsFit = true;

		// Selected keyframe: track name + raw timestamp (ticks). Empty track = nothing selected.
		String SelectedTrack;
		double SelectedKeyTime = 0.0;
		bool HasSelection = false;

		// --- Playback / preview state ---
		// The preview navigates the shared editor camera; IEditorViewport remembers this viewport's
		// view and restores it on re-entry, so orbiting here never disturbs the scene view.
		bool UsesEditorCamera() const override { return true; }

		// Playhead position, in animation ticks. Range [0, FramesAmount]. Drives both the preview
		// pose and the timeline marker.
		double PlaybackTimeTicks = 0.0;
		bool IsPlaying = true;
		bool Loop = true;
		float PlaybackSpeed = 1.0f;

		// Draw non-bone skeleton nodes in the preview (bones only by default).
		bool ShowNodes = false;

		// Recompute preview camera framing on its next update (once on first open, or on the
		// "Frame" button — re-opening restores the remembered camera instead).
		bool NeedsFraming = true;

		void OnOpened() override;
		void OnClosed() override;
		void OnPanelRegister() override;
		void OnUpdate(float deltaTime) override;

		// Applies the transport keyboard shortcuts (Space play/pause, Home/End, L loop, arrows
		// step a tick) to the shared playback state. Called by whichever panel currently has
		// keyboard focus, so the shortcuts only fire while the animation editor is focused.
		// Self-guards to run at most once per frame (both panels may call it while docked).
		void ApplyTransportShortcuts(const TUsePointer<Animation>& animation);

	private:
		// Frame index of the last ApplyTransportShortcuts pass, so two focused-reporting panels
		// can't double-apply the shortcuts in the same frame.
		int mShortcutFrame = -1;
	};
}

#endif //PLUENGINE_ANIMATIONVIEWPORT_H
