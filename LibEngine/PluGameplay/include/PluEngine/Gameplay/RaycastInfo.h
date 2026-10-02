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

    // Result of SceneWorld::ShootRaycast. Lives in gameplay (not physics) so SceneWorld and scripts
    // can name it; the physics world fills it in when it answers the "Raycast" event.
    PLU_STRUCT(PyExport)
    struct PLUGAMEPLAY_API RaycastHitInfo
    {
        REFLECTION_BODY_RAYCASTHITINFO()

        PLU_PROPERTY(PyExport, PyReadOnly)
        bool Hit = false;
        PLU_PROPERTY(PyExport, PyReadOnly)
        Vec3 HitLocation = {0.0f, 0.0f, 0.0f};

        // Not a PLU_PROPERTY: pybind11 has no caster for TUsePointer, Python goes through GetHitObject.
        TUsePointer<GameObject> HitObject;

        PLU_FUNCTION(PyExport)
        [[nodiscard]] GameObject* GetHitObject() const { return HitObject.GetRaw(); }
    };

    // Payload of SceneWorld's "Raycast" event. Start/End are world-space, in metres. Objects are
    // passed by UUID: the physics world keys its bodies by object UUID.
    struct RaycastRequest
    {
        Vec3 Start;
        Vec3 End;
        DynamicArray<UInt64> IgnoredObjectUuids;
        RaycastHitInfo Result;
    };
}

#endif //PLUENGINE_RAYCASTINFO_H
