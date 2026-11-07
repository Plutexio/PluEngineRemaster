//
// Created by Plutex on 7/5/26.
//

#ifndef PLUENGINE_SKELETON_H
#define PLUENGINE_SKELETON_H
#include "PluEngine/Core/IAssetData.h"
#include "Skeleton.generated.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/AssetTypes/Animation/BoneTransform.h"

#include <utility>

namespace Plu
{
    PLU_STRUCT()
    struct PLUASSETTYPES_API SkeletonNode
    {
        REFLECTION_BODY_SKELETONNODE()

        // Polymorphic: Children may hold SkeletonBone instances via a base pointer,
        // so a virtual destructor is required for correct destruction (and enables
        // dynamic_cast used when (de)serializing the tree).
        virtual ~SkeletonNode() = default;

        PLU_STRUCT()
        String NodeName;

        Matrix4 LocalMatrix;

        DynamicArray<TOwningPointer<SkeletonNode>> Children;
    };

    PLU_STRUCT()
    struct PLUASSETTYPES_API SkeletonBone : SkeletonNode
    {
        REFLECTION_BODY_SKELETONBONE()

        Matrix4 OffsetMatrix;
    };

    PLU_STRUCT()
    struct PLUASSETTYPES_API SkeletonAttachPoint
    {
        REFLECTION_BODY_SKELETONATTACHPOINT()

        PLU_PROPERTY()
        String AttachPointName;

        PLU_PROPERTY()
        String ParentNodeName;

        //All transforms are relative to the parent node
        PLU_PROPERTY()
        Vec3 RelativeLocation;

        PLU_PROPERTY()
        Vec3 RelativeRotation;

        // The parent-node-relative transform as a matrix: translate(RelativeLocation) *
        // rotate(RelativeRotation). Compose it with the parent node's global matrix to place the
        // attach point in world space (attach points never scale).
        [[nodiscard]] Matrix4 GetLocalMatrix() const;
    };

    // Flat, evaluation-oriented view of a skeleton, derived from RootNode and cached on the
    // asset (see Skeleton::GetPoseLayout). The node tree stays the authoring/source form —
    // importer, serialization and the editor panels keep walking it; this is what the
    // per-frame pose evaluation runs on instead, so the hot path costs no string hashing,
    // no dynamic_cast and no recursion.
    //
    // Node order is DFS pre-order, which guarantees ParentIndex[i] < i: computing global
    // transforms is a single forward loop, each node's parent already resolved. BoneSlot
    // numbers bone nodes in that same DFS order, so it matches CreateBonePalette() and
    // therefore SkeletalVertex::BoneIndices — the two orderings MUST stay in sync.
    //
    // Derived data, never serialized. Built lazily on the main thread; all arrays are POD
    // (no TOwningPointer/TUsePointer), so a built layout can be read from worker threads.
    struct PLUASSETTYPES_API SkeletonPoseLayout
    {
        DynamicArray<Int32>   ParentIndex;   // -1 for the root, otherwise < own index
        DynamicArray<Int32>   BoneSlot;      // skinning palette slot, -1 for non-bone nodes
        DynamicArray<Matrix4> LocalMatrix;   // bind-pose local as authored
        DynamicArray<Matrix4> OffsetMatrix;  // inverse bind; identity where BoneSlot < 0
        DynamicArray<String>  NodeName;      // diagnostics and name->index binding only
        HashMap<String, Int32> NameToIndex;
        UInt32 BoneCount = 0;

        // LocalMatrix decomposed once at build time — the reference (bind) pose in the form
        // everything downstream works in. Used as the fallback for nodes no animation track
        // drives. Shear in a bind matrix is dropped here (see BoneTransform::FromMatrix).
        Pose LocalBindTransform;

        [[nodiscard]] UInt64 NodeCount() const { return ParentIndex.Size(); }
        [[nodiscard]] bool IsValid() const { return !ParentIndex.IsEmpty(); }

        // Index of a node by name, or -1 when absent. Meant for binding steps (attach points,
        // pose overrides, track binding) that resolve a name once and then reuse the index.
        [[nodiscard]] Int32 FindIndex(const String& nodeName) const;

        // Copy of the bind pose, sized to this layout. Starting point for a graph that only
        // overrides some bones.
        void MakeBindPose(Pose& outPose) const;

        // Local (parent-space) pose -> skeleton-space pose. One forward pass: DFS pre-order
        // guarantees a parent is final before its children are reached. outPose may not alias
        // localPose; it is resized to this layout's node count.
        void ComposeGlobals(const Pose& localPose, Pose& outGlobalPose) const;

        // Skeleton-space pose -> the (OffsetMatrix, globalMatrix) pairs the skinning shader wants,
        // bones only, in the order CreateBonePalette uses (== SkeletalVertex::BoneIndices).
        // This is the ONE place transforms become matrices — keep it at the end of the chain.
        void BuildBonePalette(const Pose& globalPose, DynamicArray<std::pair<Matrix4, Matrix4>>& outPalette) const;

        // Per-node blend weights for layered/masked blending: rootIndex and its whole subtree get
        // insideWeight, every other node gets outsideWeight. One forward pass — DFS pre-order
        // guarantees ParentIndex[i] < i, so a node's parent is already resolved when it is reached.
        // rootIndex < 0 => the whole mask is outsideWeight. Feeds BlendPosesMasked. Lives here (not
        // on the node that needs it first) because any future masking node (bone mask, per-bone
        // additive) will want the same helper.
        void BuildSubtreeMask(Int32 rootIndex, float insideWeight, float outsideWeight,
                              DynamicArray<float>& outWeights) const;

        void Clear();
    };

    PLU_STRUCT()
    struct PLUASSETTYPES_API Skeleton : IAssetData
    {
        REFLECTION_BODY_SKELETON()

        PLU_STRUCT()
        String SkeletonName;

        TOwningPointer<SkeletonNode> RootNode;

        // Uniform scale this rig was baked at on import (SkeletalMeshImportOptions::Scale).
        // The node matrices already carry it — this is the record of *which* scale, so anything
        // later imported against this skeleton (a skinned mesh, an animation clip) can bake the
        // same one instead of relying on the user re-typing it. 1.0 for skeletons imported
        // before the field existed.
        float ImportScale = 1.0f;

        bool IsIdentical(Skeleton& other);

        // Number of SkeletonBone nodes in the hierarchy (used as a dedup tie-breaker:
        // among identical skeletons the bone-richest one is preferred).
        [[nodiscard]] UInt64 CountBones() const;

        [[nodiscard]] TUsePointer<SkeletonNode> FindNodeByName(const String& nodeName) const;

        // Builds a flat, DFS pre-order palette of *copies* of this skeleton's bones,
        // index-aligned with SkeletalVertex::BoneIndices (same ordering as the import
        // palette: SkeletonBone nodes in DFS pre-order, non-bone nodes skipped). The
        // copies are standalone (Children left empty) so per-instance animation can
        // overwrite their matrices without corrupting the shared skeleton asset.
        // outPalette is cleared first; a null pointer is ignored.
        void CreateBonePalette(DynamicArray<TOwningPointer<SkeletonBone>>* outPalette) const;

        // Builds a DFS pre-order palette of *copies* of every node (bones and plain
        // nodes alike), with the parent/child hierarchy preserved on the copies. Use
        // this as the animatable working tree — traverse Children to compute global
        // transforms without touching the asset. outPalette[0] is the root copy.
        // outPalette is cleared first; a null pointer is ignored.
        void CreateNodePalette(DynamicArray<TOwningPointer<SkeletonNode>>* outPalette) const;

        // Flat evaluation view of this skeleton, built from RootNode on first call and cached
        // for the asset's lifetime (all instances share it — it depends only on the hierarchy,
        // not on any animation or component). Main thread only; the returned layout is POD and
        // safe to read from other threads once built. Returns an empty layout when RootNode is
        // null. Call InvalidatePoseLayout after mutating the node tree in place.
        [[nodiscard]] const SkeletonPoseLayout& GetPoseLayout() const;

        // Drops the cached layout so the next GetPoseLayout rebuilds it. Only needed by code
        // that edits RootNode/Children after the skeleton has been used (import-time paths).
        void InvalidatePoseLayout() const;

        //Attach points
        HashMap<String, TOwningPointer<SkeletonAttachPoint>> AttachPoints;

    private:
        mutable SkeletonPoseLayout mPoseLayout;
        mutable bool mPoseLayoutBuilt = false;
    };
}

#endif //PLUENGINE_SKELETON_H
