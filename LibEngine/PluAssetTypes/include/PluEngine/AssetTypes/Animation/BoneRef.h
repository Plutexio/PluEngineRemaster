//
// Created by Plutex on 7/25/26.
//

#ifndef PLUENGINE_BONEREF_H
#define PLUENGINE_BONEREF_H

#include "PluEngine/Core.h"
#include "PluSTL/String/String.h"
#include "PluEngine/Core/Reflection/ReflectionBase.h"

namespace Plu
{
    struct Skeleton;

    // A reference to a skeleton node *by name*. A distinct type (not a bare String) so the editor
    // renders a bone-hierarchy picker via TypeSerializer<BoneRef> instead of a plain text field —
    // a typo in a bone name would otherwise only surface at runtime. Serialization stores just the
    // name (same pattern as CollisionProfileRef).
    struct PLUASSETTYPES_API BoneRef
    {
        String Name;
    };

    // Bone picker widget (editor-only; implemented in BoneRef.cpp, which is the only place this
    // header lets imgui/Skeleton.h leak into).
    PLUASSETTYPES_API bool BoneRefEditorControl(void* value, const String& name);

    // Skeleton the bone pickers read their node list from. There is no way for BoneRef's
    // TypeSerializer::EditorControl (a bare void*) to reach its owning node/asset, so the details
    // panel sets this for the duration of drawing a node's properties (same trick as
    // ActiveCollisionConfig() for CollisionProfileRef). Null => picker renders disabled, showing the
    // current name as-is.
    PLUASSETTYPES_API void SetBonePickerSkeleton(Skeleton* skeleton);
    PLUASSETTYPES_API Skeleton* GetBonePickerSkeleton();

    template <>
    struct TypeSerializer<BoneRef>
    {
        static nlohmann::json Serialize(void* value) { return static_cast<BoneRef*>(value)->Name.CStr(); }

        static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
        {
            if (json.is_string()) static_cast<BoneRef*>(outValue)->Name = json.get<std::string>().c_str();
        }

        static bool EditorControl(void* value, const String& name)
        {
#ifdef PLU_ENGINE_EDITOR_BUILD
            return BoneRefEditorControl(value, name);
#else
            ImGui::Text("No Editor Utils in engine! Cannot show bone picker");
            return false;
#endif
        }
    };
}

#endif //PLUENGINE_BONEREF_H
