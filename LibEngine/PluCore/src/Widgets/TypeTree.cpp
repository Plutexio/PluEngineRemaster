//
// Created by Plutex on 9/29/26.
//

#include "PluEngine/Core/Widgets/TypeTree.h"

#ifdef PLU_ENGINE_EDITOR_BUILD

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <imgui.h>

#include "PluEngine/Core/Reflection/ReflectionBase.h"
#include "PluEngine/Timer.h"
#include "UI/IconsFontAwesome7.h"

namespace Plu
{
    namespace
    {
        // Direct children of every reflected type, each bucket sorted by name. The hierarchy does not
        // depend on which root is drawn, so one cache serves every TypeTree. Rebuilt on demand: Python
        // classes register at runtime, after the first build.
        HashMap<TypeInfo*, DynamicArray<TypeInfo*>> gTypeTreeChildren;
        HashMap<TypeInfo*, UInt32>                  gTypeTreeDescendantCount;
        bool                                        gTypeTreeBuilt = false;

        void BuildTypeTree()
        {
            PLU_PROFILE_SCOPE("TypeTree::Build");

            gTypeTreeChildren.Clear();
            gTypeTreeDescendantCount.Clear();

            // Sorting the flat list first keeps every bucket sorted without touching the buckets again.
            DynamicArray<TypeInfo*> types;
            for (auto typeReg : *TypeRegistry::GetInstance()->GetTypeMap()) {
                TypeInfo* type = typeReg.second;
                if (type->BaseType && type->BaseType->Type == type->Type) {
                    types.PushBack(type);
                }
            }
            types.Sort([](TypeInfo* a, TypeInfo* b) -> bool {
                return strcmp(a->TypeName.CStr(), b->TypeName.CStr()) < 0;
            });

            for (TypeInfo* type : types) {
                if (DynamicArray<TypeInfo*>* kids = gTypeTreeChildren.Find(type->BaseType)) {
                    kids->PushBack(type);
                } else {
                    gTypeTreeChildren.Insert(type->BaseType, {type});
                }
            }
            gTypeTreeBuilt = true;
        }

        UInt32 GetDescendantCount(TypeInfo* type)
        {
            if (const UInt32* cached = gTypeTreeDescendantCount.Find(type)) {
                return *cached;
            }
            UInt32 count = 0;
            if (DynamicArray<TypeInfo*>* kids = gTypeTreeChildren.Find(type)) {
                for (TypeInfo* kid : *kids) {
                    count += 1 + GetDescendantCount(kid);
                }
            }
            gTypeTreeDescendantCount.Insert(type, count);
            return count;
        }

        struct TypeTreeDrawState
        {
            TypeInfo**             Output = nullptr;
            const ImGuiTextFilter* Filter = nullptr; // Null while the search box is empty.
            HashMap<TypeInfo*, bool> SubtreeMatchCache;
            bool                   Confirmed = false;
        };

        // True when `type` or anything below it passes the search filter.
        bool SubtreeMatches(TypeInfo* type, TypeTreeDrawState* state)
        {
            if (const bool* cached = state->SubtreeMatchCache.Find(type)) {
                return *cached;
            }
            bool match = state->Filter->PassFilter(type->TypeName.CStr());
            if (DynamicArray<TypeInfo*>* kids = gTypeTreeChildren.Find(type)) {
                // No short-circuit: every child's answer gets cached for when it is drawn.
                for (TypeInfo* kid : *kids) {
                    match |= SubtreeMatches(kid, state);
                }
            }
            state->SubtreeMatchCache.Insert(type, match);
            return match;
        }

        const char* GetTypeIcon(const TypeInfo* type)
        {
            if (type->IsAbstract) return ICON_FA_FOLDER;
            if (type->IsPythonType) return ICON_FA_CODE;
            return ICON_FA_CUBE;
        }

        void DrawTypeNode(TypeInfo* type, TypeTreeDrawState* state, bool isRoot)
        {
            if (state->Filter && !SubtreeMatches(type, state)) {
                return;
            }

            DynamicArray<TypeInfo*>* kids = gTypeTreeChildren.Find(type);
            const bool leaf       = !kids || kids->IsEmpty();
            const bool selected   = *state->Output == type;
            const bool selectable = !type->IsAbstract;
            // Abstract types, and while searching the ancestors shown only as a path to a hit, are dimmed.
            const bool dimmed = !selectable || (state->Filter && !state->Filter->PassFilter(type->TypeName.CStr()));

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                       ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_DrawLinesToNodes;
            if (leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (selected) flags |= ImGuiTreeNodeFlags_Selected;
            if (isRoot) flags |= ImGuiTreeNodeFlags_DefaultOpen;
            // A hit inside a collapsed branch would be invisible, so searching forces the path open.
            if (state->Filter && !leaf) ImGui::SetNextItemOpen(true);

            if (dimmed) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            const bool open = ImGui::TreeNodeEx(type, flags, "%s  %s", GetTypeIcon(type), type->TypeName.CStr());
            if (dimmed) ImGui::PopStyleColor();

            // Descendant count, right-aligned on the row. Drawn straight into the draw list so it does
            // not become an item and steal the node's clicks/tooltip.
            if (!leaf) {
                char badge[16];
                snprintf(badge, sizeof(badge), "%u", GetDescendantCount(type));
                const ImVec2 badgeSize = ImGui::CalcTextSize(badge);
                const ImVec2 rowMin    = ImGui::GetItemRectMin();
                const ImVec2 rowMax    = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(rowMax.x - badgeSize.x - ImGui::GetStyle().FramePadding.x,
                           rowMin.y + (rowMax.y - rowMin.y - badgeSize.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), badge);
            }

            // Click selects, a click on the current selection (so also a double-click) confirms it.
            // Arrow clicks only toggle the node.
            if (selectable && ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
                if (selected) {
                    state->Confirmed = true;
                } else {
                    *state->Output = type;
                }
            }

            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted(type->TypeName.CStr());
                if (type->BaseType) ImGui::TextDisabled("Base: %s", type->BaseType->TypeName.CStr());
                if (type->IsPythonType) ImGui::TextDisabled("Python class");
                if (!selectable) {
                    ImGui::TextDisabled("Abstract - cannot be picked");
                } else {
                    ImGui::TextDisabled(selected ? "Click again to confirm" : "Click to select, double-click to confirm");
                }
                ImGui::EndTooltip();
            }

            if (open && !leaf) {
                for (TypeInfo* kid : *kids) {
                    DrawTypeNode(kid, state, false);
                }
                ImGui::TreePop();
            }
        }
    }
}

bool Plu::ImGuiWidgets::TypeTree(TypeInfo* root, TypeInfo** output, const char* confirmLabel)
{
    PLU_PROFILE_SCOPE("ImGuiWidgets::TypeTree");

    if (!gTypeTreeBuilt) {
        BuildTypeTree();
    }

    ImGui::PushID(root);
    const ImGuiStyle& style  = ImGui::GetStyle();
    const float       startX = ImGui::GetCursorPosX();
    // Popups and menus size themselves to their content, so the widget brings its own minimum width.
    const float width = std::max(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 18.0f);

    // Search box + refresh button.
    static ImGuiTextFilter filter;
    const float buttonSize = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(width - buttonSize - style.ItemSpacing.x);
    if (ImGui::InputTextWithHint("##Search", ICON_FA_MAGNIFYING_GLASS "  Search types...", filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf))) {
        filter.Build();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE, ImVec2(buttonSize, buttonSize))) {
        BuildTypeTree();
    }
    ImGui::SetItemTooltip("Rebuild the type list (picks up newly registered Python classes)");

    // The tree, scrolling once it outgrows ~20 rows.
    TypeTreeDrawState state;
    state.Output = output;
    state.Filter = filter.IsActive() ? &filter : nullptr;

    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, ImGui::GetFrameHeight() * 20.0f));
    if (ImGui::BeginChild("##Tree", ImVec2(width, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        DrawTypeNode(root, &state, true);
        if (state.Filter && !SubtreeMatches(root, &state)) {
            ImGui::TextDisabled("No types match \"%s\"", filter.InputBuf);
        }
    }
    ImGui::EndChild();

    // Footer: the current pick, plus an explicit confirm button so double-click is not the only way.
    TypeInfo* picked = *output;
    const bool canConfirm = picked && !picked->IsAbstract && picked->IsDerivedOfOrSame(root);
    ImGui::AlignTextToFramePadding();
    if (canConfirm) {
        ImGui::Text("%s  %s", GetTypeIcon(picked), picked->TypeName.CStr());
    } else {
        ImGui::TextDisabled("Nothing selected");
    }
    if (confirmLabel) {
        const float confirmWidth = ImGui::CalcTextSize(confirmLabel, nullptr, true).x + style.FramePadding.x * 2.0f;
        ImGui::SameLine(startX + width - confirmWidth);
        ImGui::BeginDisabled(!canConfirm);
        if (ImGui::Button(confirmLabel)) {
            state.Confirmed = true;
        }
        ImGui::EndDisabled();
    }

    ImGui::PopID();
    return state.Confirmed && canConfirm;
}

#endif
