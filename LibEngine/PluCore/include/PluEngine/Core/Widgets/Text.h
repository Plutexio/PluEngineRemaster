//
// Created by Plutex on 10/26/26.
//

#ifndef PLUENGINE_WIDGETS_TEXT_H
#define PLUENGINE_WIDGETS_TEXT_H

#include "PluEngine/Core.h"

namespace Plu
{
    namespace ImGuiWidgets
    {
        /** Text centred horizontally in the content region left on the current line. */
        PLUCORE_API void TextCentered(const char* text);

        /** Text centred horizontally and vertically in the content region left from the cursor. */
        PLUCORE_API void TextCenteredBoth(const char* text);

        /**
         * Placeholder for a panel with nothing to show ("No asset loaded", "Select an emitter"): dimmed,
         * wrapped text centred in the remaining content region, with an optional large icon above it.
         * Draw it instead of the panel's content, not next to it — it takes the whole region.
         */
        PLUCORE_API void EmptyState(const char* text, const char* icon = nullptr);
    }
}


#endif //PLUENGINE_WIDGETS_TEXT_H
