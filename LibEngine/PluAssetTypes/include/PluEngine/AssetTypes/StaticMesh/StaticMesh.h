//
// Created by Plutex on 10.01.2026.
//

#ifndef PLUENGINE_STATICMESH_H
#define PLUENGINE_STATICMESH_H
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/IAssetData.h"
#include "StaticMesh.generated.h"
#include "PluEngine/Core/BoundingBox.h"

namespace Plu
{
    struct IStaticMeshCollisionData;
    // NoVirtualClass: POD wierzchołka wysyłany surowo na GPU (offsetof/sizeof) —
    // nie może dostać vtable, więc GetClass() zostaje niewirtualne.
    PLU_STRUCT(NoVirtualClass)
    struct PLUASSETTYPES_API Vertex
    {
        REFLECTION_BODY_VERTEX()

        Vec3 Position;
        UInt32 Normal;
        UInt16 UV[2];
        UInt32 Color;
        UInt32 Tangent;
    };

    PLU_STRUCT()
    struct PLUASSETTYPES_API MeshData
    {
        REFLECTION_BODY_MESHDATA()

        DynamicArray<Vertex> Vertices;
        DynamicArray<UInt32> Indices;
        UInt16 MaterialIndex;
    };

    PLU_STRUCT(PyExport)
    struct PLUASSETTYPES_API StaticMesh : IAssetData
    {
        REFLECTION_BODY_STATICMESH()

        PLU_PROPERTY()
        MeshData StaticMeshData; //This we load

        TOwningPointer<IStaticMeshCollisionData> CollisionData;
        String CollisionName;

        // GPU residency. Nothing uploads meshes until the Vulkan renderer does, so it stays false.
        PLU_PROPERTY()
        bool IsLoaded;

        PLU_PROPERTY(PyExport)
        UInt32 VertexCount;

        PLU_PROPERTY(PyExport)
        UInt32 IndexCount;
    };
}

#endif //PLUENGINE_STATICMESH_H