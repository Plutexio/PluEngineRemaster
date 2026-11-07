//
// Created by Plutex on 10/26/26.
//

#include "PluEngine/Core/Widgets/Buttons.h"

namespace Plu
{
    namespace
    {
        ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t)
        {
            return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        }

        void PushAccentButtonColors()
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGuiWidgets::GetAccentColor());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGuiWidgets::GetAccentColorHovered());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGuiWidgets::GetAccentColorActive());
        }
    }
}

ImVec4 Plu::ImGuiWidgets::GetAccentColor()
{
    return ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
}

ImVec4 Plu::ImGuiWidgets::GetAccentColorHovered()
{
    const ImVec4 accent = GetAccentColor();
    return LerpColor(accent, ImVec4(1.0f, 1.0f, 1.0f, accent.w), 0.15f);
}

ImVec4 Plu::ImGuiWidgets::GetAccentColorActive()
{
    const ImVec4 accent = GetAccentColor();
    return LerpColor(accent, ImVec4(0.0f, 0.0f, 0.0f, accent.w), 0.2f);
}

bool Plu::ImGuiWidgets::IconButton(const char* label, const ImVec2& size, const char* tooltip, EIconButtonTone tone)
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (tone == EIconButtonTone::Danger) {
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.16f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.90f, 0.22f, 0.22f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.12f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.22f));
    }
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);

    if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
    return clicked;
}

bool Plu::ImGuiWidgets::ToggleButton(const char* label, bool active, const ImVec2& size, const char* tooltip)
{
    if (active) PushAccentButtonColors();
    const bool clicked = ImGui::Button(label, size);
    if (active) ImGui::PopStyleColor(3);

    if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
    return clicked;
}

bool Plu::ImGuiWidgets::SmallToggleButton(const char* label, bool active, const char* tooltip)
{
    if (active) PushAccentButtonColors();
    const bool clicked = ImGui::SmallButton(label);
    if (active) ImGui::PopStyleColor(3);

    if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
    return clicked;
}

bool Plu::ImGuiWidgets::PrimaryButton(const char* label, const ImVec2& size)
{
    PushAccentButtonColors();
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}
