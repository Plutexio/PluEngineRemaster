//
// Created by Plutex on 10/26/26.
//

#ifndef PLUENGINE_INLINERENAME_H
#define PLUENGINE_INLINERENAME_H

#include "PluEngine/Core.h"

namespace Plu
{
    namespace ImGuiWidgets
    {
        /**
         * Edit state of a rename done in place of a list row. One per panel; which row it belongs to is
         * the panel's business (a UUID, a handle, a name).
         */
        struct PLUCORE_API InlineRenameState
        {
            char Buffer[128] = {};
            bool FocusPending = false;

            /** Copies `currentName` (truncated to the buffer) and focuses the field the next time it is drawn. */
            void Begin(const char* currentName);
        };

        /**
         * The text field of a row being renamed, full row width. Returns true on the frame editing ends:
         * Enter or a click outside (commit `state->Buffer`), or Esc — ImGui then restores the original
         * text into the buffer, so a commit that ignores unchanged names writes nothing.
         * Call it in place of the row's normal widgets while that row is being renamed.
         */
        PLUCORE_API bool InlineRenameField(InlineRenameState* state, const char* id = "##rename");
    }
}


#endif //PLUENGINE_INLINERENAME_H
