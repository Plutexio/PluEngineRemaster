//
// Created by Plutex on 10/26/26.
//

#include "PluEngine/Core/Widgets/InlineRename.h"

#include <cfloat>
#include <cstdio>
#include <imgui.h>

#include "PluEngine/Core/Widgets/Buttons.h"

void Plu::ImGuiWidgets::InlineRenameState::Begin(const char* currentName)
{
    snprintf(Buffer, sizeof(Buffer), "%s", currentName ? currentName : "");
    FocusPending = true;
}

bool Plu::ImGuiWidgets::InlineRenameField(InlineRenameState* state, const char* id)
{
    // Focus has to be requested right before the field: asking for it on the click that starts the
    // rename would focus whatever widget comes next that frame instead.
    if (state->FocusPending) {
        ImGui::SetKeyboardFocusHere();
        state->FocusPending = false;
    }

    // An accent outline tells the field apart from the rows around it.
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, GetAccentColorHovered());
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool submitted = ImGui::InputText(id, state->Buffer, sizeof(state->Buffer),
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // IsItemDeactivated catches both Esc and a click outside the field.
    return submitted || ImGui::IsItemDeactivated();
}
