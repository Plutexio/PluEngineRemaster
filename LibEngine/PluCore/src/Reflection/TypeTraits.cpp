//
// Created by Plutex on 5/23/26.
//

#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/Core/Widgets/TypeTree.h"


#ifdef PLU_ENGINE_EDITOR_BUILD

namespace
{
    // The asset widgets live in PluAssetCore, one layer up, and install themselves into
    // TypeRegistry at startup. These wrappers keep the call sites in TypeTraits.h unchanged.
    template <typename Fn, typename... Args>
    bool CallAssetHook(const Fn& hook, const char* what, Args&&... args)
    {
        if (!hook) {
            ImGui::Text("Asset layer not installed! Cannot show %s", what);
            return false;
        }
        return hook(std::forward<Args>(args)...);
    }
}

bool Plu::TUsePointerAssetUI(void* value, String name, TypeInfo* typeInfo)
{
    return CallAssetHook(TypeRegistry::GetInstance()->assetPointerControl,
                         "asset selection UI", value, name, typeInfo);
}

bool Plu::UUIDForAssetUI(void* value, String name, TypeInfo* typeInfo, PropertyInfo* propertyInfo)
{
    return CallAssetHook(TypeRegistry::GetInstance()->assetUuidControl,
                         "asset UUID UI", value, name, typeInfo, propertyInfo);
}

bool Plu::ArrayTreeEditorControl(void* arrayId, const String& name, UInt64 count,
                                  const std::function<bool(UInt64 index)>& renderElement,
                                  const std::function<void()>& addElement,
                                  const std::function<void(UInt64 index)>& removeElement)
{
    // Kubełki po 10: płaski TreeNode na element skaluje się fatalnie przy setkach/tysiącach
    // wpisów (foliage, InstancedStaticMeshComponent::Instances) - ImGui ma wtedy tysiące węzłów
    // do zrenderowania i panel detali staje się nieprzewijalną ścianą. Grupowanie po 10 trzyma
    // widoczną liczbę węzłów w ryzach, dopóki nie ma dedykowanego widgetu z wirtualizacją.
    constexpr UInt64 kChunkSize = 10;

    ImGui::PushID(arrayId);
    bool changed = false;

    // TreeNodeEx z osobnym str_id: label pojedynczo-argumentowego ImGui::TreeNode(text) JEST jego
    // ID, więc gdyby tekst zawierał "count" (zmienia się po Add/Remove), ImGui widziałby to jako
    // zupełnie inny węzeł co klatkę i zwijałby go z powrotem do stanu domyślnego (closed) - stąd
    // "Add zamyka TreeNode". str_id musi być stały; dynamiczny tekst idzie tylko przez fmt.
    if (ImGui::TreeNodeEx("##ArrayRoot", ImGuiTreeNodeFlags_None, "%s (%llu)", name.CStr(), static_cast<unsigned long long>(count)))
    {
        if (ImGui::SmallButton("+ Add")) {
            addElement();
            changed = true;
        }

        // Usuwanie odłożone do końca pętli: renderElement/removeElement mutowałyby kontener,
        // po którym ta pętla iteruje po indeksie - usunięcie w środku przesunęłoby indeksy
        // pozostałych elementów pod stopami tej samej klatki.
        Int64 pendingRemove = -1;

        for (UInt64 chunkStart = 0; chunkStart < count; chunkStart += kChunkSize) {
            const UInt64 chunkEnd = (chunkStart + kChunkSize <= count) ? (chunkStart + kChunkSize - 1) : (count - 1);

            ImGui::PushID(static_cast<int>(chunkStart));
            // Ten sam powód co przy "##ArrayRoot": chunkEnd zmienia się w ostatnim (niepełnym)
            // kubełku przy Add/Remove, więc str_id musi być stały - "##Chunk" plus już wypchnięty
            // chunkStart w ID-stacku wystarczą do unikalności między kubełkami.
            if (ImGui::TreeNodeEx("##Chunk", ImGuiTreeNodeFlags_None, "Elements %llu-%llu",
                                   static_cast<unsigned long long>(chunkStart), static_cast<unsigned long long>(chunkEnd)))
            {
                for (UInt64 index = chunkStart; index <= chunkEnd; index++) {
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::SmallButton("X")) {
                        pendingRemove = static_cast<Int64>(index);
                    }
                    ImGui::SameLine();
                    if (renderElement(index)) {
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        if (pendingRemove >= 0) {
            removeElement(static_cast<UInt64>(pendingRemove));
            changed = true;
        }

        ImGui::TreePop();
    }
    ImGui::PopID();
    return changed;
}

bool Plu::ClassPointerEditorControl(TypeInfo** type, TypeInfo* baseType, const String& name)
{
    // The pick has to survive between frames until it is confirmed, and only one combo popup can be
    // open at a time, so a single slot is enough. Re-seeded from the field every time the popup opens.
    static TypeInfo* pending = nullptr;

    bool changed = false;
    const char* preview = *type ? (*type)->TypeName.CStr() : "None";
    if (ImGui::BeginCombo(name.CStr(), preview, ImGuiComboFlags_HeightLargest))
    {
        if (ImGui::IsWindowAppearing()) {
            pending = *type;
        }
        if (ImGuiWidgets::TypeTree(baseType, &pending, "Select")) {
            changed = pending != *type;
            *type = pending;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool Plu::CollisionProfileRefEditorControl(void* value, const String& name)
{
    auto* ref = static_cast<CollisionProfileRef*>(value);
    const CollisionConfig& cfg = ActiveCollisionConfig();

    bool changed = false;
    if (ImGui::BeginCombo(name.CStr(), ref->Name.CStr()))
    {
        for (UInt32 i = 0; i < cfg.Profiles.Size(); ++i)
        {
            const bool selected = (cfg.Profiles[i].Name == ref->Name);
            if (ImGui::Selectable(cfg.Profiles[i].Name.CStr(), selected))
            {
                ref->Name = cfg.Profiles[i].Name;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

#endif
