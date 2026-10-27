//
// Created by Plutex on 7/25/26.
//

#include "PluEngine/AssetTypes/Animation/BoneRef.h"

namespace Plu
{
    namespace
    {
        Skeleton* gBonePickerSkeleton = nullptr;
    }

    void SetBonePickerSkeleton(Skeleton* skeleton) { gBonePickerSkeleton = skeleton; }
    Skeleton* GetBonePickerSkeleton() { return gBonePickerSkeleton; }
}

#ifdef PLU_ENGINE_EDITOR_BUILD

#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"
#include "PluEngine/Core/Widgets/SearchBar.h"

namespace Plu
{
    namespace
    {
        // Children-by-index view of the skeleton, built once per popup open (not per frame) from
        // SkeletonPoseLayout::ParentIndex. Cached across frames while the combo stays open; rebuilt
        // whenever the popup reappears (ImGui::IsWindowAppearing), which also covers switching
        // between different BoneRef fields.
        DynamicArray<DynamicArray<Int32>> gBonePickerChildren;

        void BuildBonePickerChildren(const SkeletonPoseLayout& layout)
        {
            gBonePickerChildren.Clear();
            gBonePickerChildren.Resize(layout.NodeCount());
            for (Int32 i = 0; i < static_cast<Int32>(layout.NodeCount()); ++i) {
                const Int32 parent = layout.ParentIndex[i];
                if (parent >= 0) gBonePickerChildren[static_cast<UInt64>(parent)].PushBack(i);
            }
        }

        // Recursive draw of the unfiltered hierarchy. Returns true (and closes the popup) once a
        // node is clicked.
        bool DrawBonePickerNode(Int32 index, const SkeletonPoseLayout& layout, BoneRef* ref)
        {
            const DynamicArray<Int32>& children = gBonePickerChildren[static_cast<UInt64>(index)];
            const bool isSelected = (ref->Name == layout.NodeName[index]);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
                | ImGuiTreeNodeFlags_DefaultOpen;
            if (children.IsEmpty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

            ImGui::PushID(index);
            const bool open = ImGui::TreeNodeEx(layout.NodeName[index].CStr(), flags);
            bool picked = false;
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                ref->Name = layout.NodeName[index];
                picked = true;
            }
            if (open && !children.IsEmpty()) {
                for (Int32 child : children) {
                    if (DrawBonePickerNode(child, layout, ref)) picked = true;
                }
            }
            if (open && !(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen)) ImGui::TreePop();
            ImGui::PopID();
            return picked;
        }
    }

    bool BoneRefEditorControl(void* value, const String& name)
    {
        auto* ref = static_cast<BoneRef*>(value);
        Skeleton* skeleton = gBonePickerSkeleton;

        if (!skeleton) {
            ImGui::BeginDisabled();
            ImGui::BeginCombo(name.CStr(), ref->Name.IsEmpty() ? "(none)" : ref->Name.CStr());
            ImGui::EndCombo();
            ImGui::EndDisabled();
            return false;
        }

        const SkeletonPoseLayout& layout = skeleton->GetPoseLayout();
        bool changed = false;

        if (ImGui::BeginCombo(name.CStr(), ref->Name.IsEmpty() ? "(none)" : ref->Name.CStr())) {
            static ImGuiTextFilter filter;
            if (ImGui::IsWindowAppearing()) {
                BuildBonePickerChildren(layout);
            }
            ImGuiWidgets::PopupSearchBar(&filter, "Search bones...");

            if (!filter.IsActive()) {
                for (Int32 i = 0; i < static_cast<Int32>(layout.NodeCount()); ++i) {
                    if (layout.ParentIndex[i] < 0 && DrawBonePickerNode(i, layout, ref)) changed = true;
                }
            } else {
                for (UInt64 i = 0; i < layout.NodeCount(); ++i) {
                    if (!filter.PassFilter(layout.NodeName[i].CStr())) continue;
                    const bool selected = (layout.NodeName[i] == ref->Name);
                    if (ImGui::Selectable(layout.NodeName[i].CStr(), selected)) {
                        ref->Name = layout.NodeName[i];
                        changed = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
            }

            if (changed) ImGui::CloseCurrentPopup();
            ImGui::EndCombo();
        }
        return changed;
    }
}

#endif
