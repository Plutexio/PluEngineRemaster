//
// Created by Plutex on 10/14/26.
//

#ifndef PLUENGINE_BLENDSPACE_H
#define PLUENGINE_BLENDSPACE_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/IAssetData.h"
#include "PluEngine/AssetTypes/Animation/SkeletalAnimation.h"
#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"
#include "PluEngine/AssetTypes/SkeletalMesh/SkeletalMesh.h"
#include "PluSTL/Array/Array.h"
#include "PluSTL/String/String.h"
#include "BlendSpace.generated.h"

namespace Plu
{
	// How many input axes a BlendSpace has. One asset type covers both — a 1D space simply ignores
	// AxisY and every sample's Position.y.
	PLU_ENUM(PyNamespace=Plu)
	enum class EBlendSpaceDimensions : UInt8
	{
		OneD,
		TwoD
	};

	PLU_STRUCT()
	struct PLUASSETTYPES_API BlendSpaceAxis
	{
		REFLECTION_BODY_BLENDSPACEAXIS()

		// Display name (canvas label, node pin label) — e.g. "Speed", "Direction". Pins themselves
		// keep the stable names "X"/"Y" so renaming an axis never breaks graph links.
		PLU_PROPERTY()
		String Name = "X";

		// Input range. Inputs outside are clamped; samples should sit inside it.
		PLU_PROPERTY()
		float Min = 0.0f;
		PLU_PROPERTY()
		float Max = 1.0f;

		// Editor-only snapping grid (number of cells across the range). No effect at runtime.
		PLU_PROPERTY()
		int GridDivisions = 4;
	};

	PLU_STRUCT()
	struct PLUASSETTYPES_API BlendSample
	{
		REFLECTION_BODY_BLENDSAMPLE()

		PLU_PROPERTY()
		TUsePointer<Animation> AnimationAsset;

		// Position in axis units (x = AxisX, y = AxisY; y is ignored by a 1D space).
		PLU_PROPERTY()
		Vec2 Position = Vec2(0.0f);

		// Plays on its own clock instead of the space's shared, length-normalized phase. For clips
		// without a step cycle to line up (idle, breathing) — syncing those to a walk's phase only
		// stretches them.
		PLU_PROPERTY()
		bool ExcludeFromSync = false;
	};

	// One sample's contribution to a blend (see BlendSpace::ComputeWeights).
	struct BlendSampleWeight
	{
		UInt32 SampleIndex = 0;
		float Weight = 0.0f;
	};

	// Triangle of sample indices from BlendSpace's 2D triangulation.
	struct BlendSpaceTriangle
	{
		UInt32 A = 0, B = 0, C = 0;
	};

	// Animations placed in a 1D or 2D parameter space (e.g. Speed, or local velocity X/Z for 8-way
	// movement). Given an input point, ComputeWeights picks the samples to blend: the two neighbours
	// in 1D, the enclosing Delaunay triangle (barycentric weights) in 2D. Played by
	// AnimBlendSpaceNode inside an AnimationGraph.
	PLU_STRUCT()
	struct PLUASSETTYPES_API BlendSpace : IAssetData
	{
		REFLECTION_BODY_BLENDSPACE()

		BlendSpace() { AxisY.Name = "Y"; }

		// Skeleton the samples are authored for (same contract as AnimationGraph::TargetSkeleton).
		PLU_PROPERTY()
		TUsePointer<Skeleton> TargetSkeleton;

		// Mesh shown by the BlendSpace editor's preview. Authoring aid only — never read at runtime.
		PLU_PROPERTY()
		TUsePointer<SkeletalMesh> PreviewMesh;

		PLU_PROPERTY()
		EBlendSpaceDimensions Dimensions = EBlendSpaceDimensions::OneD;

		PLU_PROPERTY()
		BlendSpaceAxis AxisX;

		PLU_PROPERTY()
		BlendSpaceAxis AxisY;

		PLU_PROPERTY()
		DynamicArray<BlendSample> Samples;

		[[nodiscard]] bool IsTwoD() const { return Dimensions == EBlendSpaceDimensions::TwoD; }

		// Rebuilds the derived lookup data (1D sort order, 2D Delaunay triangulation + hull) from
		// Samples/axes. Call after loading and after every edit of Samples, Dimensions or axis ranges
		// — ComputeWeights reads only the cached data. Cheap (a few dozen samples at most).
		void RebuildTriangulation();

		// Blend weights for `input` (axis units; clamped to the axis ranges), appended to outWeights
		// after clearing it. Weights are > 0 and sum to 1; empty only when there are no samples.
		// 2D inputs outside the samples' hull are projected onto the nearest hull edge; with fewer
		// than three non-collinear samples the space degrades to the nearest segment/point.
		void ComputeWeights(Vec2 input, DynamicArray<BlendSampleWeight>& outWeights) const;

		// Derived data (not serialized) — exposed for the editor canvas to draw.
		[[nodiscard]] const DynamicArray<BlendSpaceTriangle>& GetTriangles() const { return mTriangles; }

		// Bumped by RebuildTriangulation — lets consumers notice the space changed under them.
		[[nodiscard]] UInt32 GetRevision() const { return mRevision; }

		// Input clamped to the axis ranges (y forced to 0 in 1D).
		[[nodiscard]] Vec2 ClampInput(Vec2 input) const;

	private:
		// Axis-normalized position (each axis mapped to [0, 1]) — triangulation and distance tests
		// run in this space so axes with very different units (m/s vs degrees) weigh equally.
		[[nodiscard]] Vec2 Normalize(Vec2 position) const;

		void ComputeWeights1D(float x, DynamicArray<BlendSampleWeight>& outWeights) const;
		void ComputeWeights2D(Vec2 point, DynamicArray<BlendSampleWeight>& outWeights) const;

		DynamicArray<UInt32> mSortedByX;              // 1D: sample indices sorted by Position.x
		DynamicArray<BlendSpaceTriangle> mTriangles; // 2D: Delaunay triangulation
		DynamicArray<std::pair<UInt32, UInt32>> mHullEdges; // 2D: edges used by exactly one triangle
		UInt32 mRevision = 0;
	};
}

#endif //PLUENGINE_BLENDSPACE_H
