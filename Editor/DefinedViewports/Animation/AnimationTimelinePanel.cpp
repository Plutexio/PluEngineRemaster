//
// Created by Plutex on 7/7/26.
//

#include "AnimationTimelinePanel.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include "glm/gtc/quaternion.hpp"
#include "AnimationViewport.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/Animation/SkeletalAnimation.h"
#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "UI/IconsFontAwesome7.h"
#include "PluEngine/Core/Widgets/Text.h"

extern Plu::ApplicationInfo* gApplicationInfo;

Plu::String Plu::AnimationTimelinePanel::GetPanelName()
{
	return "Timeline";
}

void Plu::AnimationTimelinePanel::OnClosed()
{
}

void Plu::AnimationTimelinePanel::OnOpened()
{
	// Fit the whole clip to the panel width whenever the viewport is (re)opened.
	if (AnimationViewport* viewport = DynamicCast<AnimationViewport>(GetParentViewport()).GetRaw())
		viewport->NeedsFit = true;
}

namespace
{
	// Layout constants (screen pixels).
	constexpr float kLabelWidth  = 190.0f; // frozen track-name column
	constexpr float kRowHeight   = 22.0f;
	constexpr float kRulerHeight = 24.0f;
	constexpr float kDiamondR    = 4.0f;

	// Convert a raw keyframe timestamp (assimp ticks) to seconds. FPS<=0 → treat as seconds.
	float ToSeconds(double ticks, float fps)
	{
		return fps > 0.0f ? static_cast<float>(ticks) / fps : static_cast<float>(ticks);
	}

	// Pick a "nice" ruler step (seconds) so ticks land roughly kTargetPx apart on screen.
	float NiceTimeStep(float pixelsPerSecond)
	{
		constexpr float kTargetPx = 90.0f;
		const float raw = pixelsPerSecond > 0.0f ? kTargetPx / pixelsPerSecond : 1.0f;
		const float mag = glm::pow(10.0f, glm::floor(glm::log(glm::max(raw, 1e-6f)) / glm::log(10.0f)));
		const float norm = raw / mag; // in [1, 10)
		float nice = 10.0f;
		if (norm <= 1.0f) nice = 1.0f;
		else if (norm <= 2.0f) nice = 2.0f;
		else if (norm <= 5.0f) nice = 5.0f;
		return nice * mag;
	}

	// One timeline diamond: the track's channel keys sharing an exact timestamp. Pointers
	// alias the animation asset's channel arrays, so they stay valid for the whole frame.
	struct DisplayKey
	{
		double Ts = 0.0;
		const Plu::AnimationVectorKey* Loc   = nullptr;
		const Plu::AnimationQuatKey*   Rot   = nullptr;
		const Plu::AnimationVectorKey* Scale = nullptr;
	};

	// Merge a track's three time-sorted channel arrays into one diamond list, coalescing
	// keys with an exact-equal timestamp (import keeps such shared times bit-identical).
	DynamicArray<DisplayKey> BuildDisplayKeys(const Plu::AnimationTrack& track)
	{
		const auto& L = track.LocationKeys;
		const auto& R = track.RotationKeys;
		const auto& S = track.ScaleKeys;

		DynamicArray<DisplayKey> out;
		out.Reserve(L.Size() + R.Size() + S.Size());

		UInt64 li = 0, ri = 0, si = 0;
		while (li < L.Size() || ri < R.Size() || si < S.Size())
		{
			double ts = std::numeric_limits<double>::max();
			if (li < L.Size()) ts = glm::min(ts, L[li].Timestamp);
			if (ri < R.Size()) ts = glm::min(ts, R[ri].Timestamp);
			if (si < S.Size()) ts = glm::min(ts, S[si].Timestamp);

			DisplayKey key;
			key.Ts = ts;
			if (li < L.Size() && L[li].Timestamp == ts) key.Loc   = &L[li++];
			if (ri < R.Size() && R[ri].Timestamp == ts) key.Rot   = &R[ri++];
			if (si < S.Size() && S[si].Timestamp == ts) key.Scale = &S[si++];
			out.PushBack(key);
		}
		return out;
	}

	ImU32 KeyframeColor(const DisplayKey& kf)
	{
		const int n = (kf.Loc ? 1 : 0) + (kf.Rot ? 1 : 0) + (kf.Scale ? 1 : 0);
		if (n > 1)     return IM_COL32(240, 230, 120, 255); // mixed
		if (kf.Loc)    return IM_COL32(120, 220, 140, 255); // location
		if (kf.Rot)    return IM_COL32(120, 180, 240, 255); // rotation
		if (kf.Scale)  return IM_COL32(240, 170, 110, 255); // scale
		return IM_COL32(180, 180, 180, 255);
	}

	// One-line channel tag like "L R S" for the keys present on a frame.
	Plu::String ChannelTag(const DisplayKey& kf)
	{
		Plu::String tag;
		if (kf.Loc)        tag += "L ";
		if (kf.Rot)        tag += "R ";
		if (kf.Scale)      tag += "S ";
		if (tag.IsEmpty()) tag = "-";
		return tag;
	}

	// Full value dump for a keyframe, used by both the hover tooltip and the selection bar.
	void DrawKeyframeDetails(const char* trackName, const DisplayKey& kf, float fps)
	{
		ImGui::Text(ICON_FA_DIAMOND " %s", trackName);
		ImGui::Separator();
		ImGui::Text("Time: %.4f s   (tick %.3f)", ToSeconds(kf.Ts, fps), kf.Ts);
		ImGui::Text("Channels: %s", ChannelTag(kf).CStr());
		if (kf.Loc)
			ImGui::Text("Location: %.4f, %.4f, %.4f", kf.Loc->Value.x, kf.Loc->Value.y, kf.Loc->Value.z);
		if (kf.Rot)
		{
			const Quaternion& q = kf.Rot->Value;
			const Vec3 euler = glm::degrees(glm::eulerAngles(q));
			ImGui::Text("Rotation: q(%.3f, %.3f, %.3f, %.3f)", q.x, q.y, q.z, q.w);
			ImGui::Text("          euler(%.2f, %.2f, %.2f)", euler.x, euler.y, euler.z);
		}
		if (kf.Scale)
			ImGui::Text("Scale:    %.4f, %.4f, %.4f", kf.Scale->Value.x, kf.Scale->Value.y, kf.Scale->Value.z);
	}
}

void Plu::AnimationTimelinePanel::OnUpdate(float deltaTime)
{
	if (!BeginPanel())
	{
		EndPanel();
		return;
	}

	AnimationViewport* viewport = DynamicCast<AnimationViewport>(GetParentViewport()).GetRaw();
	TUsePointer<Animation> animation = gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());

	if (!viewport || !animation)
	{
		ImGuiWidgets::EmptyState("No animation data", ICON_FA_FILM);
		EndPanel();
		return;
	}

	const float fps = animation->FramesPerSecond;

	// --- Gather tracks into a stable, name-sorted list, and find the clip duration ---
	struct TrackRef { String Name; const AnimationTrack* Track; };
	DynamicArray<TrackRef> tracks;
	tracks.Reserve(animation->Tracks.Size());
	float durationSeconds = ToSeconds(animation->FramesAmount, fps);
	for (const auto& [name, track] : animation->Tracks)
	{
		tracks.PushBack(TrackRef{ name, &track });
		// Channel arrays are time-sorted, so the last key of each carries the extent.
		if (!track.LocationKeys.IsEmpty())
			durationSeconds = glm::max(durationSeconds, ToSeconds(track.LocationKeys.Back().Timestamp, fps));
		if (!track.RotationKeys.IsEmpty())
			durationSeconds = glm::max(durationSeconds, ToSeconds(track.RotationKeys.Back().Timestamp, fps));
		if (!track.ScaleKeys.IsEmpty())
			durationSeconds = glm::max(durationSeconds, ToSeconds(track.ScaleKeys.Back().Timestamp, fps));
	}
	tracks.Sort([](const TrackRef& a, const TrackRef& b) { return strcmp(a.Name.CStr(), b.Name.CStr()) < 0; });
	durationSeconds = glm::max(durationSeconds, 0.001f);

	// --- Info bar ---
	const char* skelName = animation->AnimationSkeleton ? animation->AnimationSkeleton->SkeletonName.CStr() : "<none>";
	ImGui::Text(ICON_FA_PERSON_RUNNING " Skeleton: %s", skelName);
	ImGui::SameLine();
	ImGui::TextDisabled("|  %llu tracks   %d frames @ %.2f fps   %.3fs",
		(unsigned long long)tracks.Size(), animation->FramesAmount, fps, durationSeconds);

	// --- Transport row (playback of the shared playhead) ---
	// Keyboard shortcuts fire only while this panel is focused (and not while typing).
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
		viewport->ApplyTransportShortcuts(animation);

	if (ImGui::Button(ICON_FA_BACKWARD_STEP "##tostart"))  // jump to start, keep play state
		viewport->PlaybackTimeTicks = 0.0;
	ImGui::SetItemTooltip("To start (Home)");
	ImGui::SameLine();
	if (ImGui::Button(viewport->IsPlaying ? ICON_FA_PAUSE "##pp" : ICON_FA_PLAY "##pp"))
		viewport->IsPlaying = !viewport->IsPlaying;
	ImGui::SetItemTooltip(viewport->IsPlaying ? "Pause (Space)" : "Play (Space)");
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_FORWARD_STEP "##toend"))  // jump to end, keep play state
		viewport->PlaybackTimeTicks = static_cast<double>(animation->FramesAmount);
	ImGui::SetItemTooltip("To end (End)");
	ImGui::SameLine();
	ImGui::Checkbox(ICON_FA_ROTATE " Loop", &viewport->Loop);
	ImGui::SetItemTooltip("Loop (L)");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	ImGui::SliderFloat("##speed", &viewport->PlaybackSpeed, 0.05f, 4.0f, ICON_FA_GAUGE_HIGH " %.2fx");
	ImGui::OpenPopupOnItemClick("##speedpresets", ImGuiPopupFlags_MouseButtonRight); // right-click the slider
	ImGui::SameLine(0.0f, 2.0f);
	if (ImGui::SmallButton(ICON_FA_CARET_DOWN "##speedmenu"))                        // or click the caret
		ImGui::OpenPopup("##speedpresets");
	ImGui::SetItemTooltip("Speed presets (or right-click the slider)");
	if (ImGui::BeginPopup("##speedpresets"))
	{
		ImGui::TextDisabled("Playback speed");
		ImGui::Separator();
		constexpr float presets[] = { 0.1f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 4.0f };
		for (const float p : presets)
		{
			char label[16];
			snprintf(label, sizeof(label), "%.2gx", p);
			if (ImGui::Selectable(label, viewport->PlaybackSpeed == p))
				viewport->PlaybackSpeed = p;
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.55f, 1.0f), "%.3fs / %.3fs",
		ToSeconds(viewport->PlaybackTimeTicks, fps), durationSeconds);

	ImGui::SetNextItemWidth(180.0f);
	ImGui::SliderFloat("##zoom", &viewport->PixelsPerSecond, 10.0f, 2000.0f, "%.0f px/s", ImGuiSliderFlags_Logarithmic);
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_ARROWS_LEFT_RIGHT_TO_LINE " Fit"))
		viewport->NeedsFit = true;

	// Persistent one-line readout of the selected keyframe (survives losing hover).
	if (viewport->HasSelection)
	{
		ImGui::SameLine();
		if (const AnimationTrack* selTrack = animation->Tracks.Find(viewport->SelectedTrack))
		{
			const DynamicArray<DisplayKey> selKeys = BuildDisplayKeys(*selTrack);
			const DisplayKey* selKf = selKeys.FindIf(
				[&](const DisplayKey& k) { return k.Ts == viewport->SelectedKeyTime; });
			if (selKf != selKeys.End())
			{
				ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f),
					"  " ICON_FA_DIAMOND " %s @ %.4fs  [%s]", viewport->SelectedTrack.CStr(),
					ToSeconds(selKf->Ts, fps), ChannelTag(*selKf).CStr());
			}
		}
	}
	ImGui::Separator();

	if (tracks.Size() == 0)
	{
		ImGui::TextDisabled("Animation has no tracks.");
		EndPanel();
		return;
	}

	// --- Timeline grid (frozen ruler across the top, frozen name column down the left) ---
	ImGui::BeginChild("##anim_timeline", ImVec2(0, 0), true,
		ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar);
	{
		const float pps = viewport->PixelsPerSecond;

		// Apply a pending "Fit" now that we know the child's visible width.
		if (viewport->NeedsFit)
		{
			const float avail = glm::max(ImGui::GetContentRegionAvail().x - kLabelWidth - 16.0f, 50.0f);
			viewport->PixelsPerSecond = glm::clamp(avail / durationSeconds, 10.0f, 2000.0f);
			viewport->NeedsFit = false;
		}

		const ImVec2 origin  = ImGui::GetCursorScreenPos();      // content-space (0,0), shifts with scroll
		const ImVec2 visible = ImGui::GetContentRegionAvail();   // inner visible size
		const float contentW = kLabelWidth + durationSeconds * pps + 40.0f;
		const float contentH = kRulerHeight + tracks.Size() * kRowHeight + 8.0f;
		ImGui::Dummy(ImVec2(contentW, contentH));                // reserve for scrolling

		const float scrollX = ImGui::GetScrollX();
		const float scrollY = ImGui::GetScrollY();
		const float frozenX = origin.x + scrollX;                // left edge of visible area (screen)
		const float frozenY = origin.y + scrollY;                // top edge of visible area (screen)
		const float laneLeft = frozenX + kLabelWidth;            // where lanes begin (screen x)
		const float laneTop  = frozenY + kRulerHeight;           // where lanes begin (screen y)

		ImDrawList* dl = ImGui::GetWindowDrawList();

		auto timeToX = [&](float sec) { return origin.x + kLabelWidth + sec * pps; };
		auto rowY    = [&](int i)     { return origin.y + kRulerHeight + i * kRowHeight; };

		const bool winHovered = ImGui::IsWindowHovered();
		const ImVec2 mouse = ImGui::GetMousePos();

		// Hover result, resolved during the lane pass and shown after it. Copied by value —
		// the per-track DisplayKey lists are loop-local, only their pointees are stable.
		bool hoverValid = false;
		DisplayKey hoverKf;
		const char* hoverTrackName = nullptr;

		// === Lane area (clipped so nothing bleeds under the frozen ruler / labels) ===
		dl->PushClipRect(ImVec2(laneLeft, laneTop),
		                 ImVec2(frozenX + visible.x, frozenY + visible.y), true);

		const float step = NiceTimeStep(pps);
		// Vertical gridlines + t=0 marker.
		for (float t = 0.0f; t <= durationSeconds + step * 0.5f; t += step)
		{
			const float x = timeToX(t);
			dl->AddLine(ImVec2(x, laneTop), ImVec2(x, frozenY + visible.y), IM_COL32(255, 255, 255, 18));
		}

		for (int i = 0; i < (int)tracks.Size(); ++i)
		{
			const TrackRef& tr = tracks[i];
			const float y0 = rowY(i);
			const float yc = y0 + kRowHeight * 0.5f;
			const bool rowSelected = viewport->HasSelection && viewport->SelectedTrack == tr.Name;

			// Row background (alternating, brighter if this row holds the selection).
			const ImU32 rowBg = rowSelected ? IM_COL32(60, 90, 120, 90)
			                                : (i & 1) ? IM_COL32(255, 255, 255, 10) : IM_COL32(0, 0, 0, 0);
			if (rowBg & IM_COL32_A_MASK)
				dl->AddRectFilled(ImVec2(laneLeft, y0), ImVec2(frozenX + visible.x, y0 + kRowHeight), rowBg);

			const DynamicArray<DisplayKey> displayKeys = BuildDisplayKeys(*tr.Track);
			for (const DisplayKey& kf : displayKeys)
			{
				const float x = timeToX(ToSeconds(kf.Ts, fps));
				// Cull keyframes scrolled out of view horizontally.
				if (x < laneLeft - kDiamondR - 2.0f || x > frozenX + visible.x + kDiamondR + 2.0f)
					continue;

				const bool isSel = rowSelected && viewport->SelectedKeyTime == kf.Ts;
				const ImU32 col = KeyframeColor(kf);
				const float r = isSel ? kDiamondR + 2.0f : kDiamondR;

				const ImVec2 p0(x, yc - r), p1(x + r, yc), p2(x, yc + r), p3(x - r, yc);
				dl->AddQuadFilled(p0, p1, p2, p3, col);
				if (isSel)
					dl->AddQuad(p0, p1, p2, p3, IM_COL32(255, 255, 255, 255), 1.5f);

				// Hover test (only inside the lane area, never under the frozen strips).
				if (winHovered && mouse.x >= laneLeft && mouse.y >= laneTop &&
				    mouse.x >= x - r - 2.0f && mouse.x <= x + r + 2.0f &&
				    mouse.y >= yc - r - 2.0f && mouse.y <= yc + r + 2.0f)
				{
					hoverValid = true;
					hoverKf = kf;
					hoverTrackName = tr.Name.CStr();
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						viewport->SelectedTrack = tr.Name;
						viewport->SelectedKeyTime = kf.Ts;
						viewport->HasSelection = true;
					}
				}
			}
		}
		dl->PopClipRect();

		// === Frozen track-name column ===
		dl->PushClipRect(ImVec2(frozenX, laneTop), ImVec2(frozenX + kLabelWidth, frozenY + visible.y), true);
		dl->AddRectFilled(ImVec2(frozenX, laneTop), ImVec2(frozenX + kLabelWidth, frozenY + visible.y),
		                  IM_COL32(24, 24, 28, 255));
		dl->AddLine(ImVec2(frozenX + kLabelWidth, laneTop), ImVec2(frozenX + kLabelWidth, frozenY + visible.y),
		            IM_COL32(255, 255, 255, 40));
		for (int i = 0; i < (int)tracks.Size(); ++i)
		{
			const float y0 = rowY(i);
			const bool rowSelected = viewport->HasSelection && viewport->SelectedTrack == tracks[i].Name;
			dl->AddText(ImVec2(frozenX + 8.0f, y0 + 3.0f),
			            rowSelected ? IM_COL32(140, 200, 255, 255) : IM_COL32(210, 210, 215, 255),
			            tracks[i].Name.CStr());
		}
		dl->PopClipRect();

		// === Frozen time ruler ===
		dl->PushClipRect(ImVec2(laneLeft, frozenY), ImVec2(frozenX + visible.x, frozenY + kRulerHeight), true);
		dl->AddRectFilled(ImVec2(laneLeft, frozenY), ImVec2(frozenX + visible.x, frozenY + kRulerHeight),
		                  IM_COL32(24, 24, 28, 255));
		dl->AddLine(ImVec2(laneLeft, frozenY + kRulerHeight), ImVec2(frozenX + visible.x, frozenY + kRulerHeight),
		            IM_COL32(255, 255, 255, 40));
		for (float t = 0.0f; t <= durationSeconds + step * 0.5f; t += step)
		{
			const float x = timeToX(t);
			dl->AddLine(ImVec2(x, frozenY + kRulerHeight - 6.0f), ImVec2(x, frozenY + kRulerHeight),
			            IM_COL32(255, 255, 255, 90));
			char buf[32];
			snprintf(buf, sizeof(buf), "%.3gs", t);
			dl->AddText(ImVec2(x + 3.0f, frozenY + 4.0f), IM_COL32(190, 190, 195, 255), buf);
		}
		dl->PopClipRect();

		// === Frozen corner (ruler × label column) ===
		dl->AddRectFilled(ImVec2(frozenX, frozenY), ImVec2(frozenX + kLabelWidth, frozenY + kRulerHeight),
		                  IM_COL32(32, 32, 38, 255));
		dl->AddText(ImVec2(frozenX + 8.0f, frozenY + 4.0f), IM_COL32(160, 160, 165, 255), "Track");

		// === Clip range markers + draggable playhead (clipped to ruler+lanes, never the labels) ===
		{
			const float clipEndSec = ToSeconds(static_cast<double>(animation->FramesAmount), fps);
			const float playSec    = ToSeconds(viewport->PlaybackTimeTicks, fps);
			const float playX      = timeToX(playSec);
			const float top        = frozenY;
			const float bottom     = frozenY + visible.y;
			const float rulerBase  = top + kRulerHeight;

			dl->PushClipRect(ImVec2(laneLeft, top), ImVec2(frozenX + visible.x, bottom), true);

			// Clip start (green) / end (red) bars — the valid sample range [0, FramesAmount].
			auto drawRangeBar = [&](float sec, ImU32 col, bool isEnd) {
				const float x = timeToX(sec);
				dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), col, 2.0f);
				const float d = 5.0f; // little inward triangle cap sitting on the ruler baseline
				if (isEnd)
					dl->AddTriangleFilled(ImVec2(x, rulerBase), ImVec2(x - d, rulerBase - d), ImVec2(x, rulerBase - d), col);
				else
					dl->AddTriangleFilled(ImVec2(x, rulerBase), ImVec2(x + d, rulerBase - d), ImVec2(x, rulerBase - d), col);
			};
			drawRangeBar(0.0f, IM_COL32(90, 220, 120, 220), false);
			drawRangeBar(clipEndSec, IM_COL32(230, 110, 90, 220), true);

			// Playhead: full-height line + a triangle handle in the ruler band.
			const ImU32 phCol = mScrubbing ? IM_COL32(255, 140, 60, 255) : IM_COL32(255, 80, 80, 255);
			dl->AddLine(ImVec2(playX, top), ImVec2(playX, bottom), phCol, 1.5f);
			const float hh = 6.0f;
			dl->AddTriangleFilled(ImVec2(playX - hh, top), ImVec2(playX + hh, top),
			                      ImVec2(playX, rulerBase - 4.0f), phCol);

			dl->PopClipRect();

			// --- Scrubbing: click the ruler strip or grab the playhead line to drag the time. ---
			const bool onRuler = winHovered && mouse.y >= top && mouse.y <= rulerBase &&
			                     mouse.x >= laneLeft && mouse.x <= frozenX + visible.x;
			const bool onPlayhead = winHovered && glm::abs(mouse.x - playX) <= 5.0f &&
			                        mouse.y >= laneTop && mouse.y <= bottom && mouse.x >= laneLeft;
			if ((onRuler || onPlayhead) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				mScrubbing = true;
			if (mScrubbing)
			{
				if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					float sec = pps > 0.0f ? (mouse.x - (origin.x + kLabelWidth)) / pps : 0.0f;
					sec = glm::clamp(sec, 0.0f, durationSeconds);
					viewport->PlaybackTimeTicks =
						glm::clamp(static_cast<double>(sec * fps), 0.0, static_cast<double>(animation->FramesAmount));
					viewport->IsPlaying = false;
				}
				else
					mScrubbing = false;
			}
		}

		// Hover tooltip (drawn last, on top of everything).
		if (hoverValid)
		{
			ImGui::BeginTooltip();
			DrawKeyframeDetails(hoverTrackName, hoverKf, fps);
			ImGui::EndTooltip();
		}
	}
	ImGui::EndChild();

	EndPanel();
}
