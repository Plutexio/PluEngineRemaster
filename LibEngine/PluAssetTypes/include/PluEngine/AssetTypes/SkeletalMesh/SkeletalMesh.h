//
// Created by Plutex on 7/5/26.
//

#ifndef PLUENGINE_SKELETALMESH_H
#define PLUENGINE_SKELETALMESH_H
#include "PluEngine/Core/IAssetData.h"
#include "SkeletalMesh.generated.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"

namespace Plu
{
    struct SkeletonBone;
    struct SkeletonNode;
    struct Skeleton;

    // Maximum number of bone influences packed per skinned vertex.
    constexpr int kMaxBoneInfluence = 4;

    // NoVirtualClass: POD wierzchołka wysyłany surowo na GPU (offsetof/sizeof) —
    // nie może dostać vtable. Baza Vertex też jest NoVirtualClass, więc hierarchia
    // pozostaje POD-em bez vtable.
    PLU_STRUCT(NoVirtualClass)
    struct PLUASSETTYPES_API SkeletalVertex : Vertex
    {
        REFLECTION_BODY_SKELETALVERTEX()

        // Skinning data filled during import: BoneIndices reference the owning skeleton's
        // bone palette (SkeletonBone nodes in DFS order), BoneWeights are normalized and
        // sum to 1 for skinned vertices. Unused slots keep index 0 / weight 0.
        UInt32 BoneIndices[kMaxBoneInfluence] = {0, 0, 0, 0};
        float  BoneWeights[kMaxBoneInfluence] = {0.0f, 0.0f, 0.0f, 0.0f};
    };

    PLU_STRUCT()
    struct PLUASSETTYPES_API SkeletalMeshData
    {
        REFLECTION_BODY_SKELETALMESHDATA()

        DynamicArray<SkeletalVertex> Vertices;
        DynamicArray<UInt32> Indices;
        UInt16 MaterialIndex;
    };

    PLU_STRUCT()
    struct PLUASSETTYPES_API SkeletalMesh : IAssetData
    {
        REFLECTION_BODY_SKELETALMESH()

        PLU_PROPERTY()
        SkeletalMeshData MeshData;

        PLU_PROPERTY()
        TUsePointer<Skeleton> MeshSkeleton;

        // GPU residency. Nothing uploads meshes until the Vulkan renderer does, so it stays false.
        PLU_PROPERTY()
        bool IsLoaded;

        PLU_PROPERTY(PyExport)
        UInt32 VertexCount;

        PLU_PROPERTY(PyExport)
        UInt32 IndexCount;
    };
}

#endif //PLUENGINE_SKELETALMESH_H
