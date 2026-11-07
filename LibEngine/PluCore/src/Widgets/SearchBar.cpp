//
// Created by Plutex on 10/26/26.
//

#include "PluEngine/Core/Widgets/SearchBar.h"

#include <algorithm>

#include "UI/IconsFontAwesome7.h"

bool Plu::ImGuiWidgets::SearchBar(const char* id, ImGuiTextFilter* filter, const char* hint, float width)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 framePadding = style.FramePadding;
    const float  iconWidth    = std::max(ImGui::CalcTextSize(ICON_FA_MAGNIFYING_GLASS).x, ImGui::CalcTextSize(ICON_FA_XMARK).x);
    // Horizontal padding is symmetric, so widening it makes room for the magnifier on the left and
    // the clear button on the right at once.
    const float sidePadding = framePadding.x + iconWidth + style.ItemInnerSpacing.x;

    ImGui::PushID(id);
    ImGui::BeginGroup();

    ImGui::SetNextItemWidth(width);
    ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F);
    // The clear button is submitted on top of the field afterwards.
    ImGui::SetNextItemAllowOverlap();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(sidePadding, framePadding.y));
    bool changed = ImGui::InputTextWithHint("##Search", hint, filter->InputBuf, IM_ARRAYSIZE(filter->InputBuf));
    ImGui::PopStyleVar();

    const bool   fieldActive = ImGui::IsItemActive();
    const ImVec2 frameMin    = ImGui::GetItemRectMin();
    const ImVec2 frameMax    = ImGui::GetItemRectMax();
    ImDrawList*  drawList    = ImGui::GetWindowDrawList();
    if (changed) filter->Build();

    const bool hasText = filter->InputBuf[0] != '\0';
    drawList->AddText(ImVec2(frameMin.x + framePadding.x, frameMin.y + framePadding.y),
                      ImGui::GetColorU32(hasText || fieldActive ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                      ICON_FA_MAGNIFYING_GLASS);

    if (hasText) {
        ImGui::SetCursorScreenPos(ImVec2(frameMax.x - sidePadding, frameMin.y));
        if (ImGui::InvisibleButton("##Clear", ImVec2(sidePadding, frameMax.y - frameMin.y))) {
            filter->Clear();
            changed = true;
        }
        const bool   clearHovered = ImGui::IsItemHovered();
        const ImVec2 clearSize    = ImGui::CalcTextSize(ICON_FA_XMARK);
        drawList->AddText(ImVec2(frameMax.x - framePadding.x - clearSize.x, frameMin.y + framePadding.y),
                          ImGui::GetColorU32(clearHovered ? ImGuiCol_Text : ImGuiCol_TextDisabled), ICON_FA_XMARK);
        ImGui::SetItemTooltip("Clear search");
    }

    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

bool Plu::ImGuiWidgets::PopupSearchBar(ImGuiTextFilter* filter, const char* hint, float width)
{
    if (ImGui::IsWindowAppearing()) {
        filter->Clear();
        ImGui::SetKeyboardFocusHere();
    }
    return SearchBar("##PopupSearch", filter, hint, width);
}
