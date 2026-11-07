//
// Created by Plutex on 9/29/26.
//

#ifndef PLUENGINE_TYPETREE_H
#define PLUENGINE_TYPETREE_H

#include "PluEngine/Core.h"

namespace Plu
{
    struct TypeInfo;
}

namespace Plu
{
    namespace ImGuiWidgets
    {
        /**
         * Type picker: a search box and a tree of `root` plus every reflected type derived from it.
         * Clicking a concrete type stores it in `*output`; abstract types are shown but cannot be picked.
         * With `confirmLabel` set, a footer button with that label confirms the pick.
         * Returns true on the frame the pick is confirmed (that button, a double-click, or a click on the
         * already selected type) — `*output` then holds it.
         * Editor build only (PLU_ENGINE_EDITOR_BUILD), like the rest of the reflection editor controls.
         */
        PLUCORE_API bool TypeTree(TypeInfo* root, TypeInfo** output, const char* confirmLabel = nullptr);
    }
}


#endif //PLUENGINE_TYPETREE_H
