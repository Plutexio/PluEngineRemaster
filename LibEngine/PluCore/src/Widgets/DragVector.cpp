//
// Created by Plutex on 10/26/26.
//

#include "PluEngine/Core/Widgets/DragVector.h"

#include <algorithm>
#include <imgui_internal.h>

namespace Plu
{
    namespace
    {
        constexpr ImVec4 kAxisColors[4] = {
            ImVec4(0.86f, 0.27f, 0.27f, 1.0f), // X
            ImVec4(0.42f, 0.76f, 0.26f, 1.0f), // Y
            ImVec4(0.27f, 0.50f, 0.92f, 1.0f), // Z
            ImVec4(0.62f, 0.62f, 0.62f, 1.0f), // W
        };
    }
}

namespace Plu
{
    namespace
    {
        // Shared layout of the float and int variants; `dragComponent(i)` draws the drag field of
        // component i (with an ID already pushed) and returns whether it changed.
        template <typename DragComponentFn>
        bool DragVectorImpl(const char* label, int components, DragComponentFn&& dragComponent)
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            if (window->SkipItems) return false;
            PLU_CORE_ASSERT(components >= 1 && components <= 4, "DragVector supports 1 to 4 components");
            components = std::clamp(components, 1, 4);

            const ImGuiStyle& style    = ImGui::GetStyle();
            ImDrawList*       drawList = ImGui::GetWindowDrawList();
            const float       barWidth = std::max(2.0f, ImGui::GetFontSize() * 0.2f);

            bool changed = false;
            ImGui::BeginGroup();
            ImGui::PushID(label);
            ImGui::PushMultiItemsWidths(components, ImGui::CalcItemWidth());
            for (int i = 0; i < components; ++i) {
                if (i > 0) ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                ImGui::PushID(i);
                changed |= dragComponent(i);
                ImGui::PopID();
                ImGui::PopItemWidth();

                // The bar goes on top of the frame's left edge, rounded with it; brighter while the field
                // is hovered or dragged.
                ImVec4 color = kAxisColors[i];
                if (!ImGui::IsItemHovered() && !ImGui::IsItemActive()) color.w = 0.75f;
                const ImVec2 frameMin = ImGui::GetItemRectMin();
                const ImVec2 frameMax = ImGui::GetItemRectMax();
                drawList->AddRectFilled(frameMin, ImVec2(frameMin.x + barWidth, frameMax.y), ImGui::GetColorU32(color),
                                        style.FrameRounding, ImDrawFlags_RoundCornersLeft);
            }
            ImGui::PopID();

            const char* labelEnd = ImGui::FindRenderedTextEnd(label);
            if (label != labelEnd) {
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                ImGui::TextEx(label, labelEnd);
            }
            ImGui::EndGroup();
            return changed;
        }
    }
}

bool Plu::ImGuiWidgets::DragVector(const char* label, float* values, int components, float speed, float min, float max,
                                   const char* format, ImGuiSliderFlags flags)
{
    return DragVectorImpl(label, components, [&](int i) {
        return ImGui::DragFloat("##v", &values[i], speed, min, max, format, flags);
    });
}

bool Plu::ImGuiWidgets::DragVector(const char* label, int* values, int components, float speed, int min, int max,
                                   const char* format, ImGuiSliderFlags flags)
{
    return DragVectorImpl(label, components, [&](int i) {
        return ImGui::DragInt("##v", &values[i], speed, min, max, format, flags);
    });
}
