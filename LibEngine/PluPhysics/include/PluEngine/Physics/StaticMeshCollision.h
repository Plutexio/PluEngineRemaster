//
// Created by Plutex on 9/7/26.
//

#ifndef PLUENGINE_STATICMESHCOLLISION_H
#define PLUENGINE_STATICMESHCOLLISION_H

#include "PluEngine/Core.h"
#include "StaticMeshCollision.generated.h"
#include <Jolt/Jolt.h>
#include "Jolt/Physics/Collision/Shape/Shape.h"

namespace Plu
{
    struct StaticMesh;

    PLU_STRUCT(Abstract)
    struct PLUPHYSICS_API IStaticMeshCollisionData
    {
        REFLECTION_BODY_ISTATICMESHCOLLISIONDATA()
    public:
        virtual ~IStaticMeshCollisionData() = default;

        virtual JPH::ShapeRefC GetShape(StaticMesh* mesh) = 0;
        virtual Vec3 GetOffset(StaticMesh* mesh, Vec3 scale) { return {0.0f, 0.0f, 0.0f}; }
    };

    PLU_STRUCT()
    struct PLUPHYSICS_API StaticMeshPerVertexCollisionData : IStaticMeshCollisionData
    {
        REFLECTION_BODY_STATICMESHPERVERTEXCOLLISIONDATA()
    public:
        JPH::ShapeRefC GetShape(StaticMesh *mesh) override;
    };

    PLU_STRUCT()
    struct PLUPHYSICS_API StaticMeshApproximateCollisionData : IStaticMeshCollisionData
    {
        REFLECTION_BODY_STATICMESHAPPROXIMATECOLLISIONDATA()
    public:
        JPH::ShapeRefC GetShape(StaticMesh *mesh) override;
    };

    PLU_STRUCT()
    struct PLUPHYSICS_API StaticMeshBoundingBoxCollisionData : IStaticMeshCollisionData
    {
        REFLECTION_BODY_STATICMESHBOUNDINGBOXCOLLISIONDATA()
    public:
        JPH::ShapeRefC GetShape(StaticMesh *mesh) override;
        Vec3 GetOffset(StaticMesh *mesh, Vec3 scale) override;
    };

    PLU_STRUCT()
    struct PLUPHYSICS_API StaticMeshCollisionSphereCollisionData : IStaticMeshCollisionData
    {
        REFLECTION_BODY_STATICMESHCOLLISIONSPHERECOLLISIONDATA()
    public:
        JPH::ShapeRefC GetShape(StaticMesh *mesh) override;
        Vec3 GetOffset(StaticMesh *mesh, Vec3 scale) override;
    };
}

#endif //PLUENGINE_STATICMESHCOLLISION_H
