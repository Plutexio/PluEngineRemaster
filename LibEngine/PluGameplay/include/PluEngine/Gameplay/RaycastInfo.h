//
// Created by Plutex on 10/1/26.
//

#ifndef PLUENGINE_RAYCASTINFO_H
#define PLUENGINE_RAYCASTINFO_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "Array/Array.h"
#include "Pointers/TUsePointer.h"
#include "RaycastInfo.generated.h"

namespace Plu
{
    class GameObject;
    class WorldComponent;

    PLU_STRUCT(PyExport)
    struct PLUGAMEPLAY_API RaycastHitInfo
    {
        REFLECTION_BODY_RAYCASTHITINFO()

        PLU_PROPERTY(PyExport, PyReadOnly)
        bool Hit = false;

        PLU_PROPERTY(PyExport, PyReadOnly)
        Vec3 HitLocation = {0.0f, 0.0f, 0.0f};

        PLU_PROPERTY(PyExport, PyReadOnly)
        Vec3 HitNormal = {0.0f, 0.0f, 0.0f};

        PLU_PROPERTY(PyExport, PyReadOnly)
        float HitDistance = 0.0f;

        PLU_PROPERTY(PyExport, PyReadOnly)
        float HitFraction = 1.0f;

        PLU_PROPERTY(PyExport, PyReadOnly)
        bool StartedInside = false;

        PLU_PROPERTY(PyExport, PyReadOnly)
        Vec3 TraceStart = {0.0f, 0.0f, 0.0f};
        PLU_PROPERTY(PyExport, PyReadOnly)
        Vec3 TraceEnd = {0.0f, 0.0f, 0.0f};

        // Not PLU_PROPERTYs: pybind11 has no caster for TUsePointer, Python goes through the getters below.
        // HitObject owns the hit body; HitWorldComponent is the collider (or static mesh) whose sub-shape was hit.
        TUsePointer<GameObject> HitObject;
        TUsePointer<WorldComponent> HitWorldComponent;

        PLU_FUNCTION(PyExport)
        [[nodiscard]] GameObject* GetHitObject() const { return HitObject.GetRaw(); }

        PLU_FUNCTION(PyExport)
        [[nodiscard]] WorldComponent* GetHitWorldComponent() const { return HitWorldComponent.GetRaw(); }
    };
    
    struct RaycastRequest
    {
        Vec3 Start;
        Vec3 End;
        DynamicArray<UInt64> IgnoredObjectUuids;
        RaycastHitInfo Result;
    };
}

#endif //PLUENGINE_RAYCASTINFO_H
