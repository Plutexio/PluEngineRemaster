//
// Created by Plutex on 10/26/26.
//

#ifndef PLUENGINE_SEARCHBAR_H
#define PLUENGINE_SEARCHBAR_H

#include <cfloat>
#include <imgui.h>

#include "PluEngine/Core.h"

namespace Plu
{
    namespace ImGuiWidgets
    {
        /**
         * Search field editing an ImGuiTextFilter (case-insensitive, "a,b" = either, "-a" = exclude).
         * A magnifier sits at the left edge, a clear button at the right while there is text; Ctrl+F
         * focuses the field while its window has focus. `width` follows SetNextItemWidth (negative =
         * align to the right edge). Rebuilds the filter itself. Returns true when the text changes.
         * The icons need the editor font (Font Awesome merged in).
         */
        PLUCORE_API bool SearchBar(const char* id, ImGuiTextFilter* filter, const char* hint = "Search...",
                                   float width = -FLT_MIN);

        /**
         * SearchBar for the top of a popup or combo dropdown: clears the filter and takes keyboard focus
         * on the frame the popup appears, so typing filters straight away.
         */
        PLUCORE_API bool PopupSearchBar(ImGuiTextFilter* filter, const char* hint = "Search...", float width = -FLT_MIN);
    }
}


#endif //PLUENGINE_SEARCHBAR_H
