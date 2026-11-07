//
// Created by Plutex on 9/8/26.
//

#include "PluEngine/Physics/StaticMeshCollision.h"

#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"

#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include "PluEngine/PluUtils.h"
#include "PluEngine/AssetTypes/MeshBounds.h"


JPH::ShapeRefC Plu::StaticMeshPerVertexCollisionData::GetShape(StaticMesh *mesh)
{
    if (!mesh) return nullptr;

    static HashMap<UInt64, JPH::ShapeRefC> shapeCache;
    if (shapeCache.Contains(mesh->Uuid)) {
        return shapeCache[mesh->Uuid];
    }

    DynamicArray<Vertex>* vertices = &mesh->StaticMeshData.Vertices;
    DynamicArray<UInt32>* indices = &mesh->StaticMeshData.Indices;
    if (vertices->IsEmpty() || indices->IsEmpty()) return nullptr;
    if (indices->Size() % 3 != 0)
    {
        PLU_CORE_ERROR("PerVertex mesh has non-triangle index count!");
        return nullptr;
    }
    JPH::TriangleList triangles;
    triangles.reserve(indices->Size() / 3);
    for (UInt32 i = 0; i + 2 < indices->Size(); i += 3)
    {
        Vec3 a = vertices->At(indices->At(i+0)).Position;
        Vec3 b = vertices->At(indices->At(i+1)).Position;
        Vec3 c = vertices->At(indices->At(i+2)).Position;
        // The importer stores triangles clockwise (aiProcess_FlipWindingOrder), while Jolt treats
        // counter-clockwise as the front face and ignores back faces in contacts. Passing the
        // indices through as-is turns the mesh inside out: a body walks in through the back
        // faces and is then held inside by the front ones. Swap b/c to restore CCW.
        triangles.push_back(JPH::Triangle(
            JPH::Float3(a.x, a.y, a.z),
            JPH::Float3(c.x, c.y, c.z),
            JPH::Float3(b.x, b.y, b.z)
        ));
    }
    JPH::MeshShapeSettings settings(triangles);
    JPH::Shape::ShapeResult result = settings.Create();
    if (result.HasError())
    {
        PLU_CORE_ERROR("PerVertex mesh shape creation failed: {}", result.GetError().c_str());
        return nullptr;
    }
    shapeCache[mesh->Uuid] = result.Get();
    return shapeCache[mesh->Uuid];
}

JPH::ShapeRefC Plu::StaticMeshApproximateCollisionData::GetShape(StaticMesh *mesh)
{
    if (!mesh) return nullptr;
    static HashMap<UInt64, JPH::ShapeRefC> shapeCache;
    if (shapeCache.Contains(mesh->Uuid))
    {
        return shapeCache[mesh->Uuid];
    }

    DynamicArray<Vertex>* vertices = &mesh->StaticMeshData.Vertices;
    DynamicArray<UInt32>* indices = &mesh->StaticMeshData.Indices;
    if (vertices->IsEmpty()) return nullptr;
    JPH::ConvexHullShapeSettings settings;
    settings.mPoints.reserve(vertices->Size());
    for (UInt32 i = 0; i < vertices->Size(); i++)
    {
        Vec3 p = vertices->At(i).Position;
        settings.mPoints.push_back(JPH::Vec3(p.x, p.y, p.z));
    }
    settings.mMaxConvexRadius = JPH::cDefaultConvexRadius;
    JPH::Shape::ShapeResult result = settings.Create();
    if (result.HasError())
    {
        PLU_CORE_ERROR("ConvexHull shape creation failed: {}", result.GetError().c_str());
        return nullptr;
    }
    shapeCache[mesh->Uuid] = result.Get();
    return shapeCache[mesh->Uuid];
}

JPH::ShapeRefC Plu::StaticMeshBoundingBoxCollisionData::GetShape(StaticMesh *mesh)
{
    BoundingBox bb = CreateBoundingBoxForStaticMesh(mesh);
    Vec3 halfExtent = bb.GetExtent();
    halfExtent.x = Plu::ClampF(halfExtent.x, 0.001f, FLT_MAX);
    halfExtent.y = Plu::ClampF(halfExtent.y, 0.001f, FLT_MAX);
    halfExtent.z = Plu::ClampF(halfExtent.z, 0.001f, FLT_MAX);

    static HashMap<Vec3, JPH::ShapeRefC> shapeCache;
    if (!shapeCache.Contains(halfExtent)) {
        shapeCache[halfExtent] = new JPH::BoxShape(JPH::Vec3(halfExtent.x, halfExtent.y, halfExtent.z));
        return shapeCache[halfExtent];
    } else {
        return shapeCache[halfExtent];
    }
}

Vec3 Plu::StaticMeshBoundingBoxCollisionData::GetOffset(StaticMesh *mesh, Vec3 scale)
{
    BoundingBox bb = CreateBoundingBoxForStaticMesh(mesh);
    return bb.GetCenter() * scale;
}

JPH::ShapeRefC Plu::StaticMeshCollisionSphereCollisionData::GetShape(StaticMesh *mesh)
{
    BoundingBox bb = CreateBoundingBoxForStaticMesh(mesh);
    Vec3 halfExtent = bb.GetExtent();
    float radius = Plu::ClampF(glm::max(halfExtent.x, glm::max(halfExtent.y, halfExtent.z)), 0.001f, FLT_MAX);

    static HashMap<float, JPH::ShapeRefC> shapeCache;
    if (!shapeCache.Contains(radius)) {
        shapeCache[radius] = new JPH::SphereShape(radius);
        return shapeCache[radius];
    } else {
        return shapeCache[radius];
    }
}

Vec3 Plu::StaticMeshCollisionSphereCollisionData::GetOffset(StaticMesh *mesh, Vec3 scale)
{
    BoundingBox bb = CreateBoundingBoxForStaticMesh(mesh);
    return bb.GetCenter() * scale;
}


