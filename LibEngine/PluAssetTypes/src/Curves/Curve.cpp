//
// Created by Plutex on 9/25/26.
//

#include "PluEngine/AssetTypes/Curves/Curve.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#ifdef PLU_ENGINE_EDITOR_BUILD
#include "imgui.h"
#endif

namespace Plu
{
	Curve Curve::Constant(float value)
	{
		return Ramp(value, value);
	}

	Curve Curve::Ramp(float a, float b)
	{
		Curve curve;
		curve.Keys.PushBack(CurveKey{0.0f, a, 0.0f, 0.0f});
		curve.Keys.PushBack(CurveKey{1.0f, b, 0.0f, 0.0f});
		return curve;
	}

	void Curve::SortKeys()
	{
		std::stable_sort(Keys.begin(), Keys.end(), [](const CurveKey& a, const CurveKey& b) { return a.Time < b.Time; });
	}

	float Curve::Evaluate(float time) const
	{
		const size_t count = Keys.Size();
		if (count == 0) return 0.0f;
		if (count == 1 || time <= Keys[0].Time) return Keys[0].Value;
		if (time >= Keys[count - 1].Time) return Keys[count - 1].Value;

		size_t i = 0;
		while (i + 2 < count && time >= Keys[i + 1].Time) ++i;
		const CurveKey& k0 = Keys[i];
		const CurveKey& k1 = Keys[i + 1];
		const float span = k1.Time - k0.Time;
		if (span <= 0.0f) return k1.Value;
		const float t = (time - k0.Time) / span;

		switch (Interp) {
			case ECurveInterp::Constant:
				return k0.Value;
			case ECurveInterp::Linear:
				return k0.Value + (k1.Value - k0.Value) * t;
			case ECurveInterp::Cubic: {
				const float t2 = t * t, t3 = t2 * t;
				return (2 * t3 - 3 * t2 + 1) * k0.Value + (t3 - 2 * t2 + t) * k0.OutTangent * span +
				       (-2 * t3 + 3 * t2) * k1.Value + (t3 - t2) * k1.InTangent * span;
			}
		}
		return k0.Value;
	}

	UInt32 Curve::BakeLUT(float inMin, float inMax, DynamicArray<float>& outSamples) const
	{
		const UInt32 count = Interp == ECurveInterp::Cubic ? kCubicLUTSamples : kLinearLUTSamples;
		for (UInt32 s = 0; s < count; ++s) {
			const float t = inMin + (inMax - inMin) * (static_cast<float>(s) / static_cast<float>(count - 1));
			outSamples.PushBack(Evaluate(t));
		}
		return count;
	}

	ColorGradient ColorGradient::Constant(const Vec4& color)
	{
		ColorGradient gradient;
		gradient.Keys.PushBack(ColorKey{0.0f, color});
		gradient.Keys.PushBack(ColorKey{1.0f, color});
		return gradient;
	}

	void ColorGradient::SortKeys()
	{
		std::stable_sort(Keys.begin(), Keys.end(), [](const ColorKey& a, const ColorKey& b) { return a.Time < b.Time; });
	}

	Vec4 ColorGradient::Evaluate(float time) const
	{
		const size_t count = Keys.Size();
		if (count == 0) return Vec4(1.0f);
		if (count == 1 || time <= Keys[0].Time) return Keys[0].Color;
		if (time >= Keys[count - 1].Time) return Keys[count - 1].Color;

		size_t i = 0;
		while (i + 2 < count && time >= Keys[i + 1].Time) ++i;
		const float span = Keys[i + 1].Time - Keys[i].Time;
		if (span <= 0.0f) return Keys[i + 1].Color;
		const float t = (time - Keys[i].Time) / span;
		return Keys[i].Color + (Keys[i + 1].Color - Keys[i].Color) * t;
	}

	UInt32 ColorGradient::BakeLUT(float inMin, float inMax, DynamicArray<float>& outSamples) const
	{
		for (UInt32 s = 0; s < Curve::kLinearLUTSamples; ++s) {
			const float t = inMin + (inMax - inMin) * (static_cast<float>(s) / static_cast<float>(Curve::kLinearLUTSamples - 1));
			const Vec4 c = Evaluate(t);
			outSamples.PushBack(c.x); outSamples.PushBack(c.y); outSamples.PushBack(c.z); outSamples.PushBack(c.w);
		}
		return Curve::kLinearLUTSamples;
	}
}

#ifdef PLU_ENGINE_EDITOR_BUILD
namespace Plu
{
	namespace
	{
		const char* VisibleLabel(const String& label)
		{
			// "##..." is the hidden-label convention: the caller already names the field.
			return (label.Length() >= 2 && label.CStr()[0] == '#' && label.CStr()[1] == '#') ? nullptr : label.CStr();
		}

		constexpr float kHitRadius = 8.0f;
		constexpr float kKeyRadius = 5.0f;

		// Smallest of 1, 2, 2.5, 5 x 10^n that is >= value.
		float NiceStep(float value)
		{
			if (value <= 0.0f) return 0.25f;
			const float magnitude = std::pow(10.0f, std::floor(std::log10(value)));
			const float normalised = value / magnitude;
			const float nice = normalised <= 1.0f ? 1.0f : normalised <= 2.0f ? 2.0f : normalised <= 2.5f ? 2.5f : normalised <= 5.0f ? 5.0f : 10.0f;
			return nice * magnitude;
		}

		// Vertical extent of a curve canvas: grid lines at multiples of Step over [GridMin, GridMax], the
		// canvas itself a little taller (Min/Max) so keys on the grid edge stay grabbable.
		struct CurveViewRange
		{
			float GridMin = 0.0f, GridMax = 1.0f, Step = 0.25f;
			float Min = 0.0f, Max = 1.0f;
		};

		// Always spans at least [0, 1] and grows only in whole grid steps. Fitting the view to the curve's own
		// min/max (the old behaviour) blew a 0.01 change up to the full canvas height, so the picture said
		// nothing about the actual values.
		CurveViewRange ComputeCurveViewRange(const Curve& curve, float includeMin = 0.0f, float includeMax = 1.0f)
		{
			float lo = std::min(0.0f, includeMin), hi = std::max(1.0f, includeMax);
			// Sampling as well as the keys: a cubic curve can overshoot its key values.
			for (int i = 0; i <= 64; ++i) {
				const float v = curve.Evaluate(static_cast<float>(i) / 64.0f);
				lo = std::min(lo, v); hi = std::max(hi, v);
			}
			for (const CurveKey& key : curve.Keys) { lo = std::min(lo, key.Value); hi = std::max(hi, key.Value); }

			CurveViewRange range;
			range.Step = NiceStep((hi - lo) / 4.0f);
			// Epsilon inwards: a value sitting on a grid line up to float noise (0.99999, 1.00001) must not
			// push the grid a whole step out.
			range.GridMin = std::floor(lo / range.Step + 1e-4f) * range.Step;
			range.GridMax = std::ceil(hi / range.Step - 1e-4f) * range.Step;
			const float pad = (range.GridMax - range.GridMin) * 0.06f;
			range.Min = range.GridMin - pad;
			range.Max = range.GridMax + pad;
			return range;
		}

		void DrawCurveInRect(ImDrawList* dl, const Curve& curve, ImVec2 p0, ImVec2 p1, float vmin, float vmax, ImU32 color, float thickness)
		{
			const int samples = 64;
			ImVec2 points[samples + 1];
			for (int i = 0; i <= samples; ++i) {
				const float t = static_cast<float>(i) / samples;
				const float v = (curve.Evaluate(t) - vmin) / (vmax - vmin);
				points[i] = ImVec2(p0.x + t * (p1.x - p0.x), p1.y - v * (p1.y - p0.y));
			}
			dl->AddPolyline(points, samples + 1, color, ImDrawFlags_None, thickness);
		}
	}

	bool CurveEditorControl(Curve& curve, const String& label)
	{
		bool changed = false;
		ImGui::PushID(&curve);
		if (const char* text = VisibleLabel(label)) ImGui::TextUnformatted(text);

		int interp = static_cast<int>(curve.Interp);
		const char* interpNames[] = { "Constant", "Linear", "Cubic" };
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::Combo("##interp", &interp, interpNames, 3)) { curve.Interp = static_cast<ECurveInterp>(interp); changed = true; }

		if (curve.Keys.Size() < 2) {
			// A curve needs two ends to be draggable; heal an empty/one-key curve instead of drawing nothing.
			const float base = curve.Keys.IsEmpty() ? 1.0f : curve.Keys[0].Value;
			curve = Curve::Ramp(base, base);
			changed = true;
		}

		const float width = std::max(ImGui::GetContentRegionAvail().x, 60.0f);
		const ImVec2 size(width, 140.0f);
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1(p0.x + size.x, p0.y + size.y);
		ImGui::InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const bool hovered = ImGui::IsItemHovered();

		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImGuiID selectedId = ImGui::GetID("selected");
		const ImGuiID draggingId = ImGui::GetID("dragging");
		const ImGuiID frozenGridMinId = ImGui::GetID("frozenGridMin");
		const ImGuiID frozenGridMaxId = ImGui::GetID("frozenGridMax");
		const ImGuiID frozenStepId = ImGui::GetID("frozenStep");
		int selected = storage->GetInt(selectedId, -1);
		bool dragging = storage->GetBool(draggingId, false);
		if (selected >= static_cast<int>(curve.Keys.Size())) selected = -1;

		// While a key is dragged the view is frozen outright and the value clamped to it. Any rescale during
		// the drag (even "only grow") feeds back: the key reaches the margin, the view grows, the same mouse
		// position now means a bigger value, the view grows again — the key runs off. The view catches up on
		// release; to go further, drag to the edge, release, drag again.
		CurveViewRange range;
		if (dragging) {
			range.GridMin = storage->GetFloat(frozenGridMinId, 0.0f);
			range.GridMax = storage->GetFloat(frozenGridMaxId, 1.0f);
			range.Step = storage->GetFloat(frozenStepId, 0.25f);
			const float pad = (range.GridMax - range.GridMin) * 0.06f;
			range.Min = range.GridMin - pad;
			range.Max = range.GridMax + pad;
		} else {
			range = ComputeCurveViewRange(curve);
		}
		const float vmin = range.Min, vmax = range.Max;
		const auto toScreen = [&](float t, float v) {
			return ImVec2(p0.x + t * size.x, p1.y - (v - vmin) / (vmax - vmin) * size.y);
		};
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const float mouseT = std::clamp((mouse.x - p0.x) / size.x, 0.0f, 1.0f);
		const float mouseV = std::clamp(vmin + (p1.y - mouse.y) / size.y * (vmax - vmin), vmin, vmax);

		int hoveredKey = -1;
		float bestDistance = kHitRadius;
		for (UInt32 i = 0; i < curve.Keys.Size(); ++i) {
			const ImVec2 p = toScreen(curve.Keys[i].Time, curve.Keys[i].Value);
			const float d = std::sqrt((p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y));
			if (d < bestDistance) { bestDistance = d; hoveredKey = static_cast<int>(i); }
		}

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			selected = hoveredKey;
			dragging = hoveredKey >= 0;
		}
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			if (hoveredKey >= 0) {
				if (curve.Keys.Size() > 2) { curve.Keys.RemoveAt(hoveredKey); selected = -1; changed = true; }
			} else {
				curve.Keys.PushBack(CurveKey{ mouseT, mouseV, 0.0f, 0.0f });
				curve.SortKeys();
				for (UInt32 i = 0; i < curve.Keys.Size(); ++i)
					if (curve.Keys[i].Time == mouseT && curve.Keys[i].Value == mouseV) selected = static_cast<int>(i);
				changed = true;
			}
		}
		if (dragging) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && selected >= 0) {
				// Clamped between the neighbours so dragging never reorders (the selection stays valid).
				const float lo = selected > 0 ? curve.Keys[selected - 1].Time : 0.0f;
				const float hi = selected + 1 < static_cast<int>(curve.Keys.Size()) ? curve.Keys[selected + 1].Time : 1.0f;
				curve.Keys[selected].Time = std::clamp(mouseT, lo, hi);
				curve.Keys[selected].Value = mouseV;
				changed = true;
			} else {
				dragging = false;
			}
		}
		storage->SetInt(selectedId, selected);
		storage->SetBool(draggingId, dragging);
		// Latched at the frame the drag starts (and kept unchanged while it lasts, see above).
		storage->SetFloat(frozenGridMinId, range.GridMin);
		storage->SetFloat(frozenGridMaxId, range.GridMax);
		storage->SetFloat(frozenStepId, range.Step);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p0, p1, IM_COL32(24, 24, 28, 255));
		for (int i = 1; i < 4; ++i) {
			const float x = p0.x + size.x * i / 4.0f;
			dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), IM_COL32(60, 60, 68, 255));
		}
		// Value grid with labels; 0 and 1 brighter, they are the lines a scale curve is read against.
		const int lineCount = static_cast<int>(std::round((range.GridMax - range.GridMin) / range.Step));
		for (int i = 0; i <= lineCount; ++i) {
			const float value = range.GridMin + range.Step * static_cast<float>(i);
			const float y = toScreen(0.0f, value).y;
			const bool major = std::abs(value) < 1e-4f || std::abs(value - 1.0f) < 1e-4f;
			dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), major ? IM_COL32(95, 95, 110, 255) : IM_COL32(60, 60, 68, 255));
			char text[16];
			std::snprintf(text, sizeof(text), "%g", value);
			dl->AddText(ImVec2(p0.x + 3.0f, y - ImGui::GetTextLineHeight()), IM_COL32(150, 150, 160, 255), text);
		}
		DrawCurveInRect(dl, curve, p0, p1, vmin, vmax, IM_COL32(255, 200, 80, 255), 2.0f);
		for (UInt32 i = 0; i < curve.Keys.Size(); ++i) {
			const ImVec2 p = toScreen(curve.Keys[i].Time, curve.Keys[i].Value);
			const bool isSelected = static_cast<int>(i) == selected;
			dl->AddCircleFilled(p, kKeyRadius, isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 200, 80, 255));
			dl->AddCircle(p, kKeyRadius, IM_COL32(0, 0, 0, 255));
		}
		dl->AddRect(p0, p1, IM_COL32(90, 90, 100, 255));

		const int readoutKey = dragging ? selected : hoveredKey;
		if (readoutKey >= 0 && (hovered || dragging))
			ImGui::SetTooltip("time %.3f\nvalue %.3f", curve.Keys[readoutKey].Time, curve.Keys[readoutKey].Value);

		if (selected >= 0) {
			CurveKey& key = curve.Keys[selected];
			float timeValue[2] = { key.Time, key.Value };
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragFloat2("##key", timeValue, 0.005f)) {
				const float lo = selected > 0 ? curve.Keys[selected - 1].Time : 0.0f;
				const float hi = selected + 1 < static_cast<int>(curve.Keys.Size()) ? curve.Keys[selected + 1].Time : 1.0f;
				key.Time = std::clamp(timeValue[0], lo, hi);
				key.Value = timeValue[1];
				changed = true;
			}
			if (curve.Interp == ECurveInterp::Cubic) {
				float tangents[2] = { key.InTangent, key.OutTangent };
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::DragFloat2("##tangents", tangents, 0.01f)) { key.InTangent = tangents[0]; key.OutTangent = tangents[1]; changed = true; }
			}
		} else {
			ImGui::TextDisabled("Right-click adds a key, right-click a key removes it");
		}

		ImGui::PopID();
		return changed;
	}

	namespace
	{
		void DrawGradientBar(ImDrawList* dl, const ColorGradient& gradient, ImVec2 p0, ImVec2 p1)
		{
			// Checker behind, so alpha is readable.
			dl->AddRectFilled(p0, p1, IM_COL32(60, 60, 60, 255));
			const float cell = (p1.y - p0.y) * 0.5f;
			for (float x = p0.x, i = 0; x < p1.x; x += cell, i += 1.0f) {
				const float x1 = std::min(x + cell, p1.x);
				dl->AddRectFilled(ImVec2(x, p0.y + (static_cast<int>(i) % 2 ? 0.0f : cell)), ImVec2(x1, p0.y + (static_cast<int>(i) % 2 ? cell : 2 * cell)), IM_COL32(90, 90, 90, 255));
			}
			const int segments = 64;
			for (int i = 0; i < segments; ++i) {
				const float t0 = static_cast<float>(i) / segments, t1 = static_cast<float>(i + 1) / segments;
				const Vec4 a = gradient.Evaluate(t0), b = gradient.Evaluate(t1);
				const ImU32 ca = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, a.w));
				const ImU32 cb = ImGui::ColorConvertFloat4ToU32(ImVec4(b.x, b.y, b.z, b.w));
				dl->AddRectFilledMultiColor(ImVec2(p0.x + t0 * (p1.x - p0.x), p0.y), ImVec2(p0.x + t1 * (p1.x - p0.x), p1.y), ca, cb, cb, ca);
			}
		}
	}

	bool GradientEditorControl(ColorGradient& gradient, const String& label)
	{
		bool changed = false;
		ImGui::PushID(&gradient);
		if (const char* text = VisibleLabel(label)) ImGui::TextUnformatted(text);

		if (gradient.Keys.Size() < 2) {
			const Vec4 base = gradient.Keys.IsEmpty() ? Vec4(1.0f) : gradient.Keys[0].Color;
			gradient = ColorGradient::Constant(base);
			changed = true;
		}

		const float width = std::max(ImGui::GetContentRegionAvail().x, 60.0f);
		const float barHeight = 22.0f, markerHeight = 14.0f;
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 barEnd(p0.x + width, p0.y + barHeight);
		ImGui::InvisibleButton("bar", ImVec2(width, barHeight + markerHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const bool hovered = ImGui::IsItemHovered();

		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const float mouseT = std::clamp((mouse.x - p0.x) / width, 0.0f, 1.0f);
		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImGuiID selectedId = ImGui::GetID("selected");
		const ImGuiID draggingId = ImGui::GetID("dragging");
		int selected = storage->GetInt(selectedId, -1);
		bool dragging = storage->GetBool(draggingId, false);
		if (selected >= static_cast<int>(gradient.Keys.Size())) selected = -1;

		int hoveredKey = -1;
		float bestDistance = kHitRadius;
		for (UInt32 i = 0; i < gradient.Keys.Size(); ++i) {
			const float d = std::abs(p0.x + gradient.Keys[i].Time * width - mouse.x);
			if (d < bestDistance) { bestDistance = d; hoveredKey = static_cast<int>(i); }
		}

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			selected = hoveredKey;
			dragging = hoveredKey >= 0;
		}
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			if (hoveredKey >= 0) {
				if (gradient.Keys.Size() > 2) { gradient.Keys.RemoveAt(hoveredKey); selected = -1; changed = true; }
			} else {
				const Vec4 color = gradient.Evaluate(mouseT);
				gradient.Keys.PushBack(ColorKey{ mouseT, color });
				gradient.SortKeys();
				for (UInt32 i = 0; i < gradient.Keys.Size(); ++i)
					if (gradient.Keys[i].Time == mouseT) selected = static_cast<int>(i);
				changed = true;
			}
		}
		if (dragging) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && selected >= 0) {
				const float lo = selected > 0 ? gradient.Keys[selected - 1].Time : 0.0f;
				const float hi = selected + 1 < static_cast<int>(gradient.Keys.Size()) ? gradient.Keys[selected + 1].Time : 1.0f;
				gradient.Keys[selected].Time = std::clamp(mouseT, lo, hi);
				changed = true;
			} else {
				dragging = false;
			}
		}
		storage->SetInt(selectedId, selected);
		storage->SetBool(draggingId, dragging);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		DrawGradientBar(dl, gradient, p0, barEnd);
		dl->AddRect(p0, barEnd, IM_COL32(90, 90, 100, 255));
		for (UInt32 i = 0; i < gradient.Keys.Size(); ++i) {
			const float x = p0.x + gradient.Keys[i].Time * width;
			const Vec4& c = gradient.Keys[i].Color;
			const bool isSelected = static_cast<int>(i) == selected;
			dl->AddTriangleFilled(ImVec2(x, barEnd.y + 1.0f), ImVec2(x - 6.0f, barEnd.y + markerHeight), ImVec2(x + 6.0f, barEnd.y + markerHeight),
			                      ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, 1.0f)));
			dl->AddTriangle(ImVec2(x, barEnd.y + 1.0f), ImVec2(x - 6.0f, barEnd.y + markerHeight), ImVec2(x + 6.0f, barEnd.y + markerHeight),
			                isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255), isSelected ? 2.0f : 1.0f);
		}

		if (selected >= 0) {
			ColorKey& key = gradient.Keys[selected];
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::ColorEdit4("##stopcolor", &key.Color.x)) changed = true;
			float time = key.Time;
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragFloat("##stoptime", &time, 0.005f, 0.0f, 1.0f, "time %.3f")) {
				const float lo = selected > 0 ? gradient.Keys[selected - 1].Time : 0.0f;
				const float hi = selected + 1 < static_cast<int>(gradient.Keys.Size()) ? gradient.Keys[selected + 1].Time : 1.0f;
				key.Time = std::clamp(time, lo, hi);
				changed = true;
			}
		} else {
			ImGui::TextDisabled("Right-click adds a stop, right-click a stop removes it");
		}

		ImGui::PopID();
		return changed;
	}

	void DrawCurveThumbnail(const Curve& curve, float width, float height)
	{
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1(p0.x + width, p0.y + height);
		ImGui::Dummy(ImVec2(width, height));
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p0, p1, IM_COL32(24, 24, 28, 255), 2.0f);
		// Same stable scale as the editor, with a faint line at 1 (no change): a thumbnail read at a glance
		// must not turn a 1 -> 0.99 curve into a full-height drop.
		const CurveViewRange range = ComputeCurveViewRange(curve);
		const float oneY = p1.y - (1.0f - range.Min) / (range.Max - range.Min) * height;
		dl->AddLine(ImVec2(p0.x, oneY), ImVec2(p1.x, oneY), IM_COL32(70, 70, 80, 255));
		DrawCurveInRect(dl, curve, p0, p1, range.Min, range.Max, IM_COL32(255, 200, 80, 255), 1.5f);
		dl->AddRect(p0, p1, IM_COL32(90, 90, 100, 255), 2.0f);
	}

	void DrawGradientThumbnail(const ColorGradient& gradient, float width, float height)
	{
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1(p0.x + width, p0.y + height);
		ImGui::Dummy(ImVec2(width, height));
		DrawGradientBar(ImGui::GetWindowDrawList(), gradient, p0, p1);
		ImGui::GetWindowDrawList()->AddRect(p0, p1, IM_COL32(90, 90, 100, 255), 2.0f);
	}
}
#else
namespace Plu
{
	bool CurveEditorControl(Curve&, const String&) { return false; }
	bool GradientEditorControl(ColorGradient&, const String&) { return false; }
	void DrawCurveThumbnail(const Curve&, float, float) {}
	void DrawGradientThumbnail(const ColorGradient&, float, float) {}
}
#endif
