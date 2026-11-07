//
// Created by Plutex on 10/14/26.
//

#include "PluEngine/AssetTypes/Animation/BlendSpace.h"

#include "PluEngine/Timer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	using Plu::BlendSampleWeight;

	constexpr float kWeightEpsilon = 1e-5f;
	// Normalized-space distance under which two samples count as the same point.
	constexpr double kCoincidentEpsilon = 1e-5;

	// Twice the signed area of (a, b, c): > 0 counter-clockwise, < 0 clockwise, ~0 collinear.
	double Orient(const Vec2& a, const Vec2& b, const Vec2& c)
	{
		return (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - a.y)
			 - (static_cast<double>(b.y) - a.y) * (static_cast<double>(c.x) - a.x);
	}

	// Whether p lies strictly inside the circumcircle of triangle (a, b, c), either winding.
	bool CircumcircleContains(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& p)
	{
		const double ax = static_cast<double>(a.x) - p.x, ay = static_cast<double>(a.y) - p.y;
		const double bx = static_cast<double>(b.x) - p.x, by = static_cast<double>(b.y) - p.y;
		const double cx = static_cast<double>(c.x) - p.x, cy = static_cast<double>(c.y) - p.y;
		const double det = (ax * ax + ay * ay) * (bx * cy - cx * by)
						 - (bx * bx + by * by) * (ax * cy - cx * ay)
						 + (cx * cx + cy * cy) * (ax * by - bx * ay);
		return Orient(a, b, c) > 0.0 ? det > 0.0 : det < 0.0;
	}

	// Parameter of the point on segment [a, b] closest to p, in [0, 1].
	float ClosestSegmentT(const Vec2& p, const Vec2& a, const Vec2& b)
	{
		const Vec2 ab = b - a;
		const float lengthSq = ab.x * ab.x + ab.y * ab.y;
		if (lengthSq <= 1e-12f) return 0.0f;
		const Vec2 ap = p - a;
		return std::clamp((ap.x * ab.x + ap.y * ab.y) / lengthSq, 0.0f, 1.0f);
	}

	float DistanceSq(const Vec2& a, const Vec2& b)
	{
		const Vec2 d = a - b;
		return d.x * d.x + d.y * d.y;
	}

	// Appends the lerp between samples i and j at t, dropping a negligible side.
	void PushSegmentWeights(UInt32 i, UInt32 j, float t, Plu::DynamicArray<BlendSampleWeight>& out)
	{
		if (t <= kWeightEpsilon) { out.PushBack({ i, 1.0f }); return; }
		if (t >= 1.0f - kWeightEpsilon) { out.PushBack({ j, 1.0f }); return; }
		out.PushBack({ i, 1.0f - t });
		out.PushBack({ j, t });
	}

	struct WorkTriangle
	{
		UInt32 A, B, C; // indices into the Bowyer–Watson vertex array
	};

	bool SameEdge(UInt32 a0, UInt32 a1, UInt32 b0, UInt32 b1)
	{
		return (a0 == b0 && a1 == b1) || (a0 == b1 && a1 == b0);
	}
}

namespace Plu
{
	Vec2 BlendSpace::ClampInput(Vec2 input) const
	{
		const auto clampAxis = [](float value, const BlendSpaceAxis& axis) {
			return std::clamp(value, std::min(axis.Min, axis.Max), std::max(axis.Min, axis.Max));
		};
		return Vec2(clampAxis(input.x, AxisX), IsTwoD() ? clampAxis(input.y, AxisY) : 0.0f);
	}

	Vec2 BlendSpace::Normalize(Vec2 position) const
	{
		const auto normalizeAxis = [](float value, const BlendSpaceAxis& axis) {
			const float range = axis.Max - axis.Min;
			return std::abs(range) > 1e-6f ? (value - axis.Min) / range : 0.0f;
		};
		return Vec2(normalizeAxis(position.x, AxisX), IsTwoD() ? normalizeAxis(position.y, AxisY) : 0.0f);
	}

	void BlendSpace::RebuildTriangulation()
	{
		PLU_PROFILE_SCOPE("BlendSpace::RebuildTriangulation");
		++mRevision;
		mSortedByX.Clear();
		mTriangles.Clear();
		mHullEdges.Clear();

		const UInt32 sampleCount = static_cast<UInt32>(Samples.Size());
		for (UInt32 i = 0; i < sampleCount; ++i) mSortedByX.PushBack(i);
		std::stable_sort(mSortedByX.Begin(), mSortedByX.End(), [this](UInt32 a, UInt32 b) {
			return Samples[a].Position.x < Samples[b].Position.x;
		});

		if (!IsTwoD() || sampleCount < 3) return;

		// Bowyer–Watson over the axis-normalized positions. Coincident samples are triangulated
		// once (the duplicates simply never get weight).
		DynamicArray<Vec2> vertices;
		DynamicArray<UInt32> vertexToSample;
		for (UInt32 i = 0; i < sampleCount; ++i)
		{
			const Vec2 point = Normalize(Samples[i].Position);
			bool duplicate = false;
			for (const Vec2& existing : vertices)
			{
				if (DistanceSq(existing, point) < kCoincidentEpsilon * kCoincidentEpsilon) { duplicate = true; break; }
			}
			if (duplicate) continue;
			vertices.PushBack(point);
			vertexToSample.PushBack(i);
		}
		const UInt32 realCount = static_cast<UInt32>(vertices.Size());
		if (realCount < 3) return;

		// Super triangle comfortably enclosing every point (samples may sit outside the axis range).
		Vec2 minP = vertices[0], maxP = vertices[0];
		for (const Vec2& v : vertices) { minP = glm::min(minP, v); maxP = glm::max(maxP, v); }
		const float extent = std::max({ maxP.x - minP.x, maxP.y - minP.y, 1.0f });
		const Vec2 mid = (minP + maxP) * 0.5f;
		vertices.PushBack(mid + Vec2(-20.0f * extent, -extent));
		vertices.PushBack(mid + Vec2(0.0f, 20.0f * extent));
		vertices.PushBack(mid + Vec2(20.0f * extent, -extent));

		DynamicArray<WorkTriangle> triangles;
		triangles.PushBack({ realCount, realCount + 1, realCount + 2 });

		DynamicArray<UInt32> bad;
		DynamicArray<std::pair<UInt32, UInt32>> boundary;
		for (UInt32 k = 0; k < realCount; ++k)
		{
			const Vec2& p = vertices[k];
			bad.Clear();
			for (UInt32 t = 0; t < triangles.Size(); ++t)
			{
				const WorkTriangle& tri = triangles[t];
				if (CircumcircleContains(vertices[tri.A], vertices[tri.B], vertices[tri.C], p)) bad.PushBack(t);
			}

			// The cavity's boundary: edges of bad triangles not shared with another bad triangle.
			boundary.Clear();
			for (UInt32 b = 0; b < bad.Size(); ++b)
			{
				const WorkTriangle& tri = triangles[bad[b]];
				const UInt32 edges[3][2] = { { tri.A, tri.B }, { tri.B, tri.C }, { tri.C, tri.A } };
				for (const auto& edge : edges)
				{
					bool shared = false;
					for (UInt32 o = 0; o < bad.Size() && !shared; ++o)
					{
						if (o == b) continue;
						const WorkTriangle& other = triangles[bad[o]];
						shared = SameEdge(edge[0], edge[1], other.A, other.B)
							  || SameEdge(edge[0], edge[1], other.B, other.C)
							  || SameEdge(edge[0], edge[1], other.C, other.A);
					}
					if (!shared) boundary.PushBack({ edge[0], edge[1] });
				}
			}

			// Remove bad triangles back to front so the remaining indices stay valid.
			for (UInt64 b = bad.Size(); b-- > 0;) triangles.RemoveAt(bad[b]);
			for (const auto& edge : boundary) triangles.PushBack({ edge.first, edge.second, k });
		}

		for (const WorkTriangle& tri : triangles)
		{
			if (tri.A >= realCount || tri.B >= realCount || tri.C >= realCount) continue;
			const double orient = Orient(vertices[tri.A], vertices[tri.B], vertices[tri.C]);
			if (std::abs(orient) < 1e-9) continue; // sliver from (near-)collinear samples
			BlendSpaceTriangle out;
			out.A = vertexToSample[tri.A];
			// Store counter-clockwise so consumers can rely on the winding.
			out.B = vertexToSample[orient > 0.0 ? tri.B : tri.C];
			out.C = vertexToSample[orient > 0.0 ? tri.C : tri.B];
			mTriangles.PushBack(out);
		}

		// Hull = edges used by exactly one triangle.
		for (UInt32 t = 0; t < mTriangles.Size(); ++t)
		{
			const BlendSpaceTriangle& tri = mTriangles[t];
			const UInt32 edges[3][2] = { { tri.A, tri.B }, { tri.B, tri.C }, { tri.C, tri.A } };
			for (const auto& edge : edges)
			{
				UInt32 uses = 0;
				for (const BlendSpaceTriangle& other : mTriangles)
				{
					if (SameEdge(edge[0], edge[1], other.A, other.B)
					 || SameEdge(edge[0], edge[1], other.B, other.C)
					 || SameEdge(edge[0], edge[1], other.C, other.A)) ++uses;
				}
				if (uses == 1) mHullEdges.PushBack({ edge[0], edge[1] });
			}
		}
	}

	void BlendSpace::ComputeWeights(Vec2 input, DynamicArray<BlendSampleWeight>& outWeights) const
	{
		outWeights.Clear();
		if (Samples.IsEmpty()) return;
		if (Samples.Size() == 1)
		{
			outWeights.PushBack({ 0, 1.0f });
			return;
		}

		const Vec2 clamped = ClampInput(input);
		if (IsTwoD()) ComputeWeights2D(Normalize(clamped), outWeights);
		else          ComputeWeights1D(clamped.x, outWeights);
	}

	void BlendSpace::ComputeWeights1D(float x, DynamicArray<BlendSampleWeight>& outWeights) const
	{
		// Samples edited without a RebuildTriangulation: sort locally rather than read stale indices.
		DynamicArray<UInt32> localOrder;
		const DynamicArray<UInt32>* order = &mSortedByX;
		if (mSortedByX.Size() != Samples.Size())
		{
			for (UInt32 i = 0; i < Samples.Size(); ++i) localOrder.PushBack(i);
			std::stable_sort(localOrder.Begin(), localOrder.End(), [this](UInt32 a, UInt32 b) {
				return Samples[a].Position.x < Samples[b].Position.x;
			});
			order = &localOrder;
		}

		const DynamicArray<UInt32>& sorted = *order;
		if (x <= Samples[sorted.Front()].Position.x) { outWeights.PushBack({ sorted.Front(), 1.0f }); return; }
		if (x >= Samples[sorted.Back()].Position.x)  { outWeights.PushBack({ sorted.Back(), 1.0f });  return; }

		for (UInt64 k = 0; k + 1 < sorted.Size(); ++k)
		{
			const UInt32 a = sorted[k];
			const UInt32 b = sorted[k + 1];
			const float xa = Samples[a].Position.x;
			const float xb = Samples[b].Position.x;
			if (x < xa || x > xb) continue;
			const float span = xb - xa;
			PushSegmentWeights(a, b, span > 1e-6f ? (x - xa) / span : 0.0f, outWeights);
			return;
		}
	}

	void BlendSpace::ComputeWeights2D(Vec2 point, DynamicArray<BlendSampleWeight>& outWeights) const
	{
		const UInt32 sampleCount = static_cast<UInt32>(Samples.Size());
		const auto valid = [sampleCount](UInt32 index) { return index < sampleCount; };

		// Inside the triangulation: barycentric weights of the enclosing triangle.
		for (const BlendSpaceTriangle& tri : mTriangles)
		{
			if (!valid(tri.A) || !valid(tri.B) || !valid(tri.C)) continue;
			const Vec2 a = Normalize(Samples[tri.A].Position);
			const Vec2 b = Normalize(Samples[tri.B].Position);
			const Vec2 c = Normalize(Samples[tri.C].Position);
			const double area = Orient(a, b, c);
			if (std::abs(area) < 1e-12) continue;
			const float wa = static_cast<float>(Orient(point, b, c) / area);
			const float wb = static_cast<float>(Orient(a, point, c) / area);
			const float wc = 1.0f - wa - wb;
			if (wa < -kWeightEpsilon || wb < -kWeightEpsilon || wc < -kWeightEpsilon) continue;

			const float ca = std::max(wa, 0.0f), cb = std::max(wb, 0.0f), cc = std::max(wc, 0.0f);
			const float sum = ca + cb + cc;
			if (ca / sum > kWeightEpsilon) outWeights.PushBack({ tri.A, ca / sum });
			if (cb / sum > kWeightEpsilon) outWeights.PushBack({ tri.B, cb / sum });
			if (cc / sum > kWeightEpsilon) outWeights.PushBack({ tri.C, cc / sum });
			return;
		}

		// Outside the hull (or no triangulation: < 3 non-collinear samples) — snap to the nearest
		// edge. Candidate edges are the hull when there is one, otherwise every sample pair. Ties
		// (collinear samples: every enclosing pair is equally close) go to the shortest segment, so
		// the result is the two immediate neighbours.
		float bestDistance = std::numeric_limits<float>::max();
		float bestLength = std::numeric_limits<float>::max();
		UInt32 bestI = 0, bestJ = 0;
		float bestT = 0.0f;
		bool found = false;
		const auto consider = [&](UInt32 i, UInt32 j) {
			const Vec2 a = Normalize(Samples[i].Position);
			const Vec2 b = Normalize(Samples[j].Position);
			const float t = ClosestSegmentT(point, a, b);
			const float distance = DistanceSq(point, a + (b - a) * t);
			const float length = DistanceSq(a, b);
			const bool closer = distance < bestDistance - 1e-9f;
			const bool tie = std::abs(distance - bestDistance) <= 1e-9f && length < bestLength;
			if (!found || closer || tie)
			{
				found = true;
				bestDistance = distance;
				bestLength = length;
				bestI = i; bestJ = j; bestT = t;
			}
		};

		if (!mTriangles.IsEmpty())
		{
			for (const auto& edge : mHullEdges)
			{
				if (valid(edge.first) && valid(edge.second)) consider(edge.first, edge.second);
			}
		}
		if (!found)
		{
			for (UInt32 i = 0; i < sampleCount; ++i)
				for (UInt32 j = i + 1; j < sampleCount; ++j)
					consider(i, j);
		}
		if (!found)
		{
			outWeights.PushBack({ 0, 1.0f });
			return;
		}
		PushSegmentWeights(bestI, bestJ, bestT, outWeights);
	}
}
