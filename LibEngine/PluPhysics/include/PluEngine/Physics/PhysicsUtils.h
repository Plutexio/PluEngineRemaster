//
// Created by Plutex on 8/12/26.
//

#ifndef PLUENGINE_PHYSICSUTILS_H
#define PLUENGINE_PHYSICSUTILS_H
#include "Jolt/Jolt.h"
#include "Jolt/Math/Real.h"
#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/Collision/Shape/CompoundShape.h"

#include "PluEngine/PluTypes.h"

namespace Plu
{
    // Jolt <-> GLM conversions. They live here, not in PluUtils, because including Jolt from a
    // public PluCore header means every module in the engine pulls it in and the bottom layer has
    // to link a physics library it never uses.
    //
    // Kept inline: they are two field copies each, called per body per frame.
    inline JPH::RVec3 ToJPH(const Vec3& V) {
        return {V.x, V.y, V.z};
    }

    inline Vec3 ToGLM(const JPH::RVec3& V) {
        return {V.GetX(), V.GetY(), V.GetZ()};
    }

    inline JPH::Vec3 ToJPHVec3(const Vec3& V) {
        return {V.x, V.y, V.z};
    }

    inline Vec3 ToGLMFromVec3(const JPH::Vec3& V) {
        return {V.GetX(), V.GetY(), V.GetZ()};
    }

    inline JPH::Quat ToJPHRotation(Vec3 rotationDegrees)
    {
        return JPH::Quat::sEulerAngles(JPH::Vec3(
            JPH::DegreesToRadians(rotationDegrees.x),
            JPH::DegreesToRadians(rotationDegrees.y),
            JPH::DegreesToRadians(rotationDegrees.z)
        ));
    }

    // Collider index (compound sub-shape user data) of the sub-shape hit on a body.
    inline UInt32 GetUserData(const JPH::Body& body, const JPH::SubShapeID& subShapeId)
    {
        // Jolt is built without RTTI, so dynamic_cast on its shapes does not link - check the type tag instead.
        const JPH::Shape *shape = body.GetShape();
        // StaticCompoundShapeSettings::Create collapses a single sub-shape into the shape itself (or a
        // RotatedTranslatedShape), so a body that is not a compound has exactly one collider: index 0.
        if (shape->GetType() != JPH::EShapeType::Compound) return 0;
        const JPH::CompoundShape *compound = static_cast<const JPH::CompoundShape *>(shape);
        JPH::SubShapeID remainder;
        return compound->GetSubShape(compound->GetSubShapeIndexFromID(subShapeId, remainder)).mUserData;
    }
}

#endif //PLUENGINE_PHYSICSUTILS_H
