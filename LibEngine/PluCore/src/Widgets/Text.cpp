//
// Created by Plutex on 10/26/26.
//

#include "PluEngine/Core/Widgets/Text.h"

#include <algorithm>
#include <imgui.h>

void Plu::ImGuiWidgets::TextCentered(const char* text)
{
    const float avail     = ImGui::GetContentRegionAvail().x;
    const float textWidth = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - textWidth) * 0.5f));
    ImGui::TextUnformatted(text);
}

void Plu::ImGuiWidgets::TextCenteredBoth(const char* text)
{
    const ImVec2 avail    = ImGui::GetContentRegionAvail();
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 cursor   = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cursor.x + std::max(0.0f, (avail.x - textSize.x) * 0.5f),
                               cursor.y + std::max(0.0f, (avail.y - textSize.y) * 0.5f)));
    ImGui::TextUnformatted(text);
}

void Plu::ImGuiWidgets::EmptyState(const char* text, const char* icon)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 avail  = ImGui::GetContentRegionAvail();
    const ImVec2 cursor = ImGui::GetCursorPos();

    // Keep lines short enough to read as a centred block, but never narrower than a few words.
    const float wrapWidth = std::max(avail.x * 0.85f, ImGui::GetFontSize() * 8.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(text, nullptr, false, wrapWidth);

    const float iconFontSize = ImGui::GetFontSize() * 2.2f;
    ImVec2 iconSize(0.0f, 0.0f);
    if (icon) {
        ImGui::PushFont(nullptr, iconFontSize);
        iconSize = ImGui::CalcTextSize(icon);
        ImGui::PopFont();
    }
    const float blockHeight = textSize.y + (icon ? iconSize.y + style.ItemSpacing.y : 0.0f);
    // Slightly above the middle reads as centred; exact centre looks low.
    float y = cursor.y + std::max(0.0f, (avail.y - blockHeight) * 0.45f);

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (icon) {
        ImGui::SetCursorPos(ImVec2(cursor.x + std::max(0.0f, (avail.x - iconSize.x) * 0.5f), y));
        ImGui::PushFont(nullptr, iconFontSize);
        ImGui::TextUnformatted(icon);
        ImGui::PopFont();
        y += iconSize.y + style.ItemSpacing.y;
    }
    const float textX = cursor.x + std::max(0.0f, (avail.x - textSize.x) * 0.5f);
    ImGui::SetCursorPos(ImVec2(textX, y));
    ImGui::PushTextWrapPos(textX + wrapWidth);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}
