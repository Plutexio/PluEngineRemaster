//
// Created by Plutex on 10/14/26.
//

#ifndef PLUENGINE_BLENDSPACEDRAWING_H
#define PLUENGINE_BLENDSPACEDRAWING_H

#include <algorithm>
#include <cmath>

#include "imgui.h"
#include "PluEngine/AssetTypes/Animation/BlendSpace.h"

namespace Plu
{
	// Maps a BlendSpace's axis units onto a screen rectangle: X left→right over [AxisX.Min, Max],
	// Y bottom→top over [AxisY.Min, Max]. A 1D space draws on the rectangle's horizontal centre line.
	// Shared by the BlendSpace editor canvas and the graph node thumbnail so both read the same.
	struct BlendSpaceScreenMapping
	{
		const BlendSpace* Space = nullptr;
		ImVec2 Min, Max;

		[[nodiscard]] ImVec2 ToScreen(Vec2 value) const
		{
			const float tx = Normalize(value.x, Space->AxisX);
			const float ty = Space->IsTwoD() ? Normalize(value.y, Space->AxisY) : 0.5f;
			return ImVec2(Min.x + tx * (Max.x - Min.x), Max.y - ty * (Max.y - Min.y));
		}

		[[nodiscard]] Vec2 FromScreen(ImVec2 point) const
		{
			const float tx = (Max.x > Min.x) ? (point.x - Min.x) / (Max.x - Min.x) : 0.0f;
			const float ty = (Max.y > Min.y) ? (Max.y - point.y) / (Max.y - Min.y) : 0.0f;
			return Vec2(Space->AxisX.Min + tx * (Space->AxisX.Max - Space->AxisX.Min),
			            Space->IsTwoD() ? Space->AxisY.Min + ty * (Space->AxisY.Max - Space->AxisY.Min) : 0.0f);
		}

	private:
		static float Normalize(float value, const BlendSpaceAxis& axis)
		{
			const float range = axis.Max - axis.Min;
			return std::abs(range) > 1e-6f ? (value - axis.Min) / range : 0.5f;
		}
	};

	namespace BlendSpaceColors
	{
		constexpr ImU32 Background   = IM_COL32(24, 24, 28, 255);
		constexpr ImU32 GridLine     = IM_COL32(60, 60, 68, 255);
		constexpr ImU32 Border       = IM_COL32(90, 90, 100, 255);
		constexpr ImU32 Triangle     = IM_COL32(120, 120, 140, 140);
		constexpr ImU32 Sample       = IM_COL32(255, 200, 80, 255);
		constexpr ImU32 SampleNoSync = IM_COL32(120, 190, 255, 255); // ExcludeFromSync
		constexpr ImU32 SampleEmpty  = IM_COL32(230, 80, 80, 255);   // no animation assigned
		constexpr ImU32 Selected     = IM_COL32(255, 255, 255, 255);
		constexpr ImU32 Preview      = IM_COL32(110, 230, 120, 255);
	}

	[[nodiscard]] inline ImU32 BlendSampleColor(const BlendSample& sample)
	{
		if (!sample.AnimationAsset) return BlendSpaceColors::SampleEmpty;
		return sample.ExcludeFromSync ? BlendSpaceColors::SampleNoSync : BlendSpaceColors::Sample;
	}

	// Triangulation edges (2D) or the axis line (1D), then the samples as dots. No interaction.
	inline void DrawBlendSpaceStructure(ImDrawList* drawList, const BlendSpaceScreenMapping& mapping, float sampleRadius)
	{
		const BlendSpace& space = *mapping.Space;
		if (space.IsTwoD()) {
			const UInt64 count = space.Samples.Size();
			for (const BlendSpaceTriangle& tri : space.GetTriangles()) {
				if (tri.A >= count || tri.B >= count || tri.C >= count) continue;
				drawList->AddTriangle(mapping.ToScreen(space.Samples[tri.A].Position),
				                      mapping.ToScreen(space.Samples[tri.B].Position),
				                      mapping.ToScreen(space.Samples[tri.C].Position),
				                      BlendSpaceColors::Triangle);
			}
		} else {
			const float y = (mapping.Min.y + mapping.Max.y) * 0.5f;
			drawList->AddLine(ImVec2(mapping.Min.x, y), ImVec2(mapping.Max.x, y), BlendSpaceColors::Triangle, 2.0f);
		}
		for (const BlendSample& sample : space.Samples)
			drawList->AddCircleFilled(mapping.ToScreen(sample.Position), sampleRadius, BlendSampleColor(sample), 12);
	}

	// Small read-only picture of a blend space — used inside the AnimBlendSpaceNode on the graph canvas.
	inline void DrawBlendSpaceThumbnail(const BlendSpace& space, float width, float height)
	{
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1(p0.x + width, p0.y + height);
		ImGui::Dummy(ImVec2(width, height));
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(p0, p1, BlendSpaceColors::Background, 2.0f);
		const float inset = 5.0f;
		BlendSpaceScreenMapping mapping{ &space, ImVec2(p0.x + inset, p0.y + inset), ImVec2(p1.x - inset, p1.y - inset) };
		DrawBlendSpaceStructure(drawList, mapping, 2.5f);
		drawList->AddRect(p0, p1, BlendSpaceColors::Border, 2.0f);
	}
}

#endif //PLUENGINE_BLENDSPACEDRAWING_H
