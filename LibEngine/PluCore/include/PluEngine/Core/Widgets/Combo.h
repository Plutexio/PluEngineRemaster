//
// Created by Plutex on 10/13/26.
//

#ifndef PLUENGINE_COMBO_H
#define PLUENGINE_COMBO_H

#include <concepts>
#include <type_traits>
#include <imgui.h>

#include "PluEngine/Core.h"
#include "PluSTL/Array/Array.h"
#include "PluSTL/String/String.h"

namespace Plu
{
    namespace ImGuiWidgets
    {
        namespace Detail
        {
            template<typename T>
            concept HasCStr = requires(const T& value) { { value.CStr() } -> std::convertible_to<const char*>; };

            template<typename T>
            concept ComboLabelLike = std::convertible_to<const T&, const char*> || HasCStr<T>;

            // `const char*`-likes go through as is, anything with CStr() (String, BasicString<char>) through CStr().
            template<ComboLabelLike T>
            const char* ComboLabelCStr(const T& label)
            {
                if constexpr (std::convertible_to<const T&, const char*>) {
                    const char* text = label;
                    return text ? text : "";
                } else {
                    return label.CStr();
                }
            }

            // A label getter takes `const T&` and returns something ComboLabelCStr accepts (a returned
            // String temporary is fine — it is kept alive for the item it labels).
            template<typename Getter, typename T>
            concept ComboLabelGetter = std::invocable<Getter&, const T&>
                && ComboLabelLike<std::remove_cvref_t<std::invoke_result_t<Getter&, const T&>>>;

            struct DefaultComboLabel
            {
                template<ComboLabelLike T>
                const char* operator()(const T& item) const { return ComboLabelCStr(item); }
            };
        }

        /**
         * Combo over a DynamicArray, picked by index. `*currentIndex` is the selected item; -1 (or any
         * out-of-range value) means nothing is selected.
         * `getLabel(const T&)` names each item; it may return `const char*` or `String`.
         * With `noneLabel` set, the list starts with an extra entry of that name which sets `*currentIndex`
         * to -1; it is also the preview while nothing is selected.
         * `emptyPreview` overrides that preview without adding a list entry (defaults to `noneLabel`).
         * Items are ID-scoped by index, so duplicate labels are fine.
         * Returns true on the frame the selection changes.
         */
        template<typename T, typename Allocator, typename LabelGetter>
            requires Detail::ComboLabelGetter<LabelGetter, T>
        bool Combo(const char* label, Int32* currentIndex, const DynamicArray<T, Allocator>& items,
                   LabelGetter&& getLabel, const char* noneLabel = nullptr, ImGuiComboFlags flags = 0,
                   const char* emptyPreview = nullptr)
        {
            const Int32 count    = static_cast<Int32>(items.Size());
            const bool  hasValue = *currentIndex >= 0 && *currentIndex < count;

            bool changed = false;
            auto drawCombo = [&](const char* preview) {
                if (!ImGui::BeginCombo(label, preview, flags)) return;

                if (noneLabel) {
                    if (ImGui::Selectable(noneLabel, !hasValue) && hasValue) {
                        *currentIndex = -1;
                        changed = true;
                    }
                    if (!hasValue) ImGui::SetItemDefaultFocus();
                }

                for (Int32 i = 0; i < count; ++i) {
                    decltype(auto) itemLabel = getLabel(items[i]);
                    const bool selected = i == *currentIndex;
                    ImGui::PushID(i);
                    if (ImGui::Selectable(Detail::ComboLabelCStr(itemLabel), selected) && !selected) {
                        *currentIndex = i;
                        changed = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            };

            if (hasValue) {
                // The preview label may be a temporary String — keep it alive for the whole combo.
                decltype(auto) previewLabel = getLabel(items[*currentIndex]);
                drawCombo(Detail::ComboLabelCStr(previewLabel));
            } else {
                drawCombo(emptyPreview ? emptyPreview : noneLabel ? noneLabel : "");
            }
            return changed;
        }

        /** Index-based Combo for arrays of `const char*` / `String` — the items are their own labels. */
        template<typename T, typename Allocator>
            requires Detail::ComboLabelLike<T>
        bool Combo(const char* label, Int32* currentIndex, const DynamicArray<T, Allocator>& items,
                   const char* noneLabel = nullptr, ImGuiComboFlags flags = 0,
                   const char* emptyPreview = nullptr)
        {
            return Combo(label, currentIndex, items, Detail::DefaultComboLabel{}, noneLabel, flags, emptyPreview);
        }

        /**
         * Combo over a DynamicArray, picked by value: the current item is the first one equal to `*value`,
         * and a pick copies the item into `*value`. A value missing from `items` shows `emptyPreview` (or
         * `noneLabel`, or an empty preview). With `noneLabel` set, its entry resets `*value` to `T{}`.
         * Returns true on the frame `*value` changes.
         */
        template<typename T, typename Allocator, typename LabelGetter>
            requires Detail::ComboLabelGetter<LabelGetter, T>
        bool ComboValue(const char* label, T* value, const DynamicArray<T, Allocator>& items,
                        LabelGetter&& getLabel, const char* noneLabel = nullptr, ImGuiComboFlags flags = 0,
                        const char* emptyPreview = nullptr)
        {
            static_assert(std::equality_comparable<T>, "ComboValue needs T == T to find the current item");

            Int32 index = -1;
            for (Int32 i = 0; i < static_cast<Int32>(items.Size()); ++i) {
                if (items[i] == *value) { index = i; break; }
            }

            if (!Combo(label, &index, items, getLabel, noneLabel, flags, emptyPreview)) return false;

            if (index >= 0) {
                *value = items[index];
            } else {
                static_assert(std::is_default_constructible_v<T>, "ComboValue with a noneLabel needs a default-constructible T");
                *value = T{};
            }
            return true;
        }

        /** Value-based Combo for arrays of `const char*` / `String` — the items are their own labels. */
        template<typename T, typename Allocator>
            requires Detail::ComboLabelLike<T>
        bool ComboValue(const char* label, T* value, const DynamicArray<T, Allocator>& items,
                        const char* noneLabel = nullptr, ImGuiComboFlags flags = 0,
                        const char* emptyPreview = nullptr)
        {
            return ComboValue(label, value, items, Detail::DefaultComboLabel{}, noneLabel, flags, emptyPreview);
        }
    }
}


#endif //PLUENGINE_COMBO_H
