//
// Created by Plutex on 10/26/26.
//

#ifndef PLUENGINE_DRAGVECTOR_H
#define PLUENGINE_DRAGVECTOR_H

#include <imgui.h>

#include "PluEngine/Core.h"
#include "PluBase/Types.h"

namespace Plu
{
    namespace ImGuiWidgets
    {
        /**
         * DragFloat per component (1-4) on one line, each field marked with its axis colour: a bar at the
         * left edge, X red, Y green, Z blue, W grey. Splits the item width like DragFloat3; the visible
         * part of `label` is drawn after the fields. `min == max` means unclamped (as in DragFloat).
         * Returns true on any frame a component changes.
         */
        PLUCORE_API bool DragVector(const char* label, float* values, int components, float speed = 0.1f,
                                    float min = 0.0f, float max = 0.0f, const char* format = "%.3f",
                                    ImGuiSliderFlags flags = 0);

        /** Integer variant of DragVector (DragInt per component), same layout and axis colours. */
        PLUCORE_API bool DragVector(const char* label, int* values, int components, float speed = 1.0f,
                                    int min = 0, int max = 0, const char* format = "%d",
                                    ImGuiSliderFlags flags = 0);

        inline bool DragVec2(const char* label, Vec2* value, float speed = 0.1f, float min = 0.0f, float max = 0.0f,
                             const char* format = "%.3f", ImGuiSliderFlags flags = 0)
        {
            return DragVector(label, &value->x, 2, speed, min, max, format, flags);
        }

        inline bool DragVec3(const char* label, Vec3* value, float speed = 0.1f, float min = 0.0f, float max = 0.0f,
                             const char* format = "%.3f", ImGuiSliderFlags flags = 0)
        {
            return DragVector(label, &value->x, 3, speed, min, max, format, flags);
        }

        inline bool DragVec4(const char* label, Vec4* value, float speed = 0.1f, float min = 0.0f, float max = 0.0f,
                             const char* format = "%.3f", ImGuiSliderFlags flags = 0)
        {
            return DragVector(label, &value->x, 4, speed, min, max, format, flags);
        }

        inline bool DragIVec2(const char* label, IVec2* value, float speed = 1.0f, int min = 0, int max = 0,
                              const char* format = "%d", ImGuiSliderFlags flags = 0)
        {
            return DragVector(label, &value->x, 2, speed, min, max, format, flags);
        }

        inline bool DragIVec3(const char* label, IVec3* value, float speed = 1.0f, int min = 0, int max = 0,
                              const char* format = "%d", ImGuiSliderFlags flags = 0)
        {
            return DragVector(label, &value->x, 3, speed, min, max, format, flags);
        }

        inline bool DragIVec4(const char* label, IVec4* value, float speed = 1.0f, int min = 0, int max = 0,
                              const char* format = "%d", ImGuiSliderFlags flags = 0)
        {
            return DragVector(label, &value->x, 4, speed, min, max, format, flags);
        }
    }
}


#endif //PLUENGINE_DRAGVECTOR_H
