//
// Created by Plutex on 10/26/26.
//

#ifndef PLUENGINE_BUTTONS_H
#define PLUENGINE_BUTTONS_H

#include <imgui.h>

#include "PluEngine/Core.h"

namespace Plu
{
    namespace ImGuiWidgets
    {
        enum class EIconButtonTone
        {
            Neutral, // Light overlay on hover.
            Danger   // Red on hover — close / delete.
        };

        /**
         * Frameless button: no fill until hovered, then an overlay. For icon clusters on toolbars and
         * title bars and for controls drawn over a viewport image. `tooltip` (optional) shows on hover.
         * Text colour is left to the caller (PushStyleColor(ImGuiCol_Text, ...)).
         */
        PLUCORE_API bool IconButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f),
                                    const char* tooltip = nullptr, EIconButtonTone tone = EIconButtonTone::Neutral);

        /**
         * Button that shows an on/off state: filled with the theme accent while `active`.
         * For tool pickers (gizmo mode, view toggles). Returns true when clicked — flipping the state is up to the caller.
         */
        PLUCORE_API bool ToggleButton(const char* label, bool active, const ImVec2& size = ImVec2(0.0f, 0.0f),
                                      const char* tooltip = nullptr);

        /** ToggleButton with SmallButton metrics (no vertical frame padding), to sit in a line of SmallButtons. */
        PLUCORE_API bool SmallToggleButton(const char* label, bool active, const char* tooltip = nullptr);

        /** Accent-filled button for the main action of a dialog or screen (Create, Save All, Open...). */
        PLUCORE_API bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f));

        /**
         * The theme accent (ImGuiCol_CheckMark — the theme keeps it for small highlights), and its
         * hovered / pressed variants, for widgets that fill with it.
         */
        PLUCORE_API ImVec4 GetAccentColor();
        PLUCORE_API ImVec4 GetAccentColorHovered();
        PLUCORE_API ImVec4 GetAccentColorActive();
    }
}


#endif //PLUENGINE_BUTTONS_H
