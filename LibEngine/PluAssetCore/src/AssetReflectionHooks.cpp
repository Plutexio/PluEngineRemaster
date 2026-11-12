//
// Created by Plutex on 8/12/26.
//
// The asset-facing half of reflection. Reflection lives in PluCore, below the asset system, so
// it cannot name EngineAssetManager; these implementations live here instead and are installed
// into TypeRegistry once at startup by InstallAssetReflectionHooks().

#include "PluEngine/AssetCore/AssetReflectionHooks.h"

#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/Core/Widgets/SearchBar.h"
#include "UI/IconsFontAwesome7.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

#ifdef PLU_ENGINE_EDITOR_BUILD
namespace Plu
{
namespace
{
bool TUsePointerAssetUI_Impl(void *value, String name, TypeInfo *typeInfo)
{
    TUsePointer<EngineAssetManager> assetManager = TypeRegistry::GetInstance()->GetAssetManager();
    TUsePointer<EngineObjectManager> engineObjectManager = TypeRegistry::GetInstance()->GetObjectManager();

    static HashMap<String, DynamicArray<TUsePointer<AssetDescriptor>>> allAssetsPerField;
    String mapKey = name + typeInfo->TypeName + String::FromInt(reinterpret_cast<UInt64>(value));
    if (!allAssetsPerField.Contains(mapKey)) {
        allAssetsPerField[mapKey] = assetManager->GetAllAssetDescriptorsOfType(typeInfo);
    }
    TUsePointer<AssetDescriptor> selected = nullptr;
    for (auto i : allAssetsPerField[mapKey]) {
        if (!static_cast<TUsePointer<IAssetData>*>(value)->GetRaw()) continue;
        if (static_cast<TUsePointer<IAssetData>*>(value)->GetRaw()->Uuid == i->Uuid) {
            selected = i;
        }
    }

	String preview = "Nothing Selected!";
	if (selected)
	{
		preview = selected->AssetName;
	}
	if (ImGui::BeginCombo(name.CStr(), preview.CStr(), 0))
    {
        // Refresh lives inside the dropdown to keep the row uncluttered.
        static ImGuiTextFilter filter;
        const float refreshSize = ImGui::GetFrameHeight();
        ImGuiWidgets::PopupSearchBar(&filter, "Search...", -(refreshSize + ImGui::GetStyle().ItemSpacing.x));
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE "##Refresh", ImVec2(refreshSize, refreshSize))) {
            allAssetsPerField[mapKey] = assetManager->GetAllAssetDescriptorsOfType(typeInfo);
        }
        ImGui::SetItemTooltip("Refresh the list");

		bool is_selected = selected == nullptr;

		bool changed = false;

		if (ImGui::Selectable("NULL", is_selected)) {
		    *static_cast<TUsePointer<IAssetData>*>(value) = nullptr;
		    changed = true;
		}


        for (int n = 0; n < allAssetsPerField[mapKey].Size(); n++)
        {
            TUsePointer<AssetDescriptor> asset = allAssetsPerField[mapKey][n];
            IPropertyInfo* nameProp = asset->GetClass()->FindProperty("Name");
            String objName;
            void* namePtr = nameProp ? nameProp->GetPtr(asset.GetRaw()) : nullptr;
            if (namePtr) {
                objName = *static_cast<String *>(namePtr);
            } else {
                objName = asset->AssetName;
            }
            is_selected = (asset == selected);
            if (filter.PassFilter(objName.CStr()))
                if (ImGui::Selectable(objName.CStr(), is_selected)) {
                    selected = asset;
                    *static_cast<TUsePointer<IAssetData>*>(value) = assetManager->GetAssetData(selected);
                    changed = true;
                }
        }
        ImGui::EndCombo();
		return changed;
    }
    return false;
}

bool UUIDForAssetUI_Impl(void* value, String name, TypeInfo* typeInfo, PropertyInfo* propertyInfo)
{
    static HashMap<String, DynamicArray<EngineObjectHandle>> objectsPerUuidField;
    static HashMap<String, DynamicArray<TUsePointer<AssetDescriptor>>> assetsPerUuidField;

    TUsePointer<EngineAssetManager> assetManager = TypeRegistry::GetInstance()->GetAssetManager();
    TUsePointer<EngineObjectManager> engineObjectManager = TypeRegistry::GetInstance()->GetObjectManager();

    bool changed = false;

    const bool isAsset = propertyInfo->UuidForClass->IsDerivedOfOrSame(IAssetData::GetStaticClass());

    // Cache the candidate list per target class, not per property name: two
    // unrelated properties that happen to share a name must not collide.
    const String cacheKey = propertyInfo->UuidForClass->TypeName;

    if (isAsset) {
        if (!assetsPerUuidField.Contains(cacheKey)) {
            assetsPerUuidField[cacheKey] = assetManager->GetAllAssetDescriptorsOfType(propertyInfo->UuidForClass);
        }
    } else {
        if (!objectsPerUuidField.Contains(cacheKey)) {
            objectsPerUuidField[cacheKey] = engineObjectManager->GetAllObjectsOfClass(propertyInfo->UuidForClass);
        }
    }

    // The UUID currently stored in the property is the single source of truth
    // for what is selected — never an ephemeral per-frame UI index.
    const UInt64 currentUuid = static_cast<PluUUID*>(value)->getUUID();

    if (isAsset) {
        DynamicArray<TUsePointer<AssetDescriptor>>& assets = assetsPerUuidField[cacheKey];

        String preview = "Nothing selected!";
        for (int n = 0; n < assets.Size(); n++) {
            if (assets[n] && assets[n]->Uuid == currentUuid) {
                preview = assets[n]->AssetName;
                break;
            }
        }

        if (ImGui::BeginCombo(propertyInfo->PropertyName.CStr(), preview.CStr(), 0))
        {
            // Refresh lives inside the dropdown to keep the row uncluttered.
            static ImGuiTextFilter filter;
            const float refreshSize = ImGui::GetFrameHeight();
            ImGuiWidgets::PopupSearchBar(&filter, "Search...", -(refreshSize + ImGui::GetStyle().ItemSpacing.x));
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_ROTATE "##Refresh", ImVec2(refreshSize, refreshSize))) {
                assets = assetManager->GetAllAssetDescriptorsOfType(propertyInfo->UuidForClass);
            }
            ImGui::SetItemTooltip("Refresh the list");

            for (int n = 0; n < assets.Size(); n++)
            {
                TUsePointer<AssetDescriptor> asset = assets[n];
                if (!asset) continue;
                String objName = asset->AssetName;
                const bool is_selected = (asset->Uuid == currentUuid);
                if (filter.PassFilter(objName.CStr()))
                    if (ImGui::Selectable(objName.CStr(), is_selected)) {
                        *static_cast<PluUUID*>(value) = asset->Uuid;
                        changed = true;
                    }
            }
            ImGui::EndCombo();
        }
    } else {
        DynamicArray<EngineObjectHandle>& objects = objectsPerUuidField[cacheKey];

        String preview = "Nothing selected!";
        for (int n = 0; n < objects.Size(); n++) {
            TUsePointer<EngineObject> obj = engineObjectManager->GetObjectAsUser<EngineObject>(objects[n]);
            if (!obj) continue;
            void* uuidPropPtr = obj->GetClass()->GetTypeUuidProp()->GetPtr(obj.GetRaw());
            if (static_cast<PluUUID*>(uuidPropPtr)->getUUID() == currentUuid) {
                IPropertyInfo* nameProp = obj->GetClass()->FindProperty("Name");
                void* namePtr = nameProp ? nameProp->GetPtr(obj.GetRaw()) : nullptr;
                preview = namePtr ? *static_cast<String*>(namePtr) : obj->GetClass()->TypeName;
                break;
            }
        }

        if (ImGui::BeginCombo(propertyInfo->PropertyName.CStr(), preview.CStr(), 0))
        {
            // Refresh lives inside the dropdown to keep the row uncluttered.
            static ImGuiTextFilter filter;
            const float refreshSize = ImGui::GetFrameHeight();
            ImGuiWidgets::PopupSearchBar(&filter, "Search...", -(refreshSize + ImGui::GetStyle().ItemSpacing.x));
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_ROTATE "##Refresh", ImVec2(refreshSize, refreshSize))) {
                objects = engineObjectManager->GetAllObjectsOfClass(propertyInfo->UuidForClass);
            }
            ImGui::SetItemTooltip("Refresh the list");

            for (int n = 0; n < objects.Size(); n++)
            {
                TUsePointer<EngineObject> obj = engineObjectManager->GetObjectAsUser<EngineObject>(objects[n]);
                if (!obj) continue;
                IPropertyInfo* nameProp = obj->GetClass()->FindProperty("Name");
                void* namePtr = nameProp ? nameProp->GetPtr(obj.GetRaw()) : nullptr;
                String objName = namePtr ? *static_cast<String*>(namePtr) : obj->GetClass()->TypeName;

                void* uuidPropPtr = obj->GetClass()->GetTypeUuidProp()->GetPtr(obj.GetRaw());
                const UInt64 objUuid = static_cast<PluUUID*>(uuidPropPtr)->getUUID();
                const bool is_selected = (objUuid == currentUuid);
                if (filter.PassFilter(objName.CStr()))
                    if (ImGui::Selectable(objName.CStr(), is_selected)) {
                        *static_cast<PluUUID*>(value) = objUuid;
                        changed = true;
                    }
            }
            ImGui::EndCombo();
        }
    }
    return changed;
}
}
} // namespace Plu
#endif

void Plu::InstallAssetReflectionHooks()
{
    TypeRegistry* registry = TypeRegistry::GetInstance();

    registry->assetDataResolver = [](DeserializationContext* dc, const PluUUID& uuid) {
        return dc->assetManager ? dc->assetManager->GetAssetData(uuid) : TUsePointer<IAssetData>(nullptr);
    };

#ifdef PLU_ENGINE_EDITOR_BUILD
    registry->assetPointerControl = &TUsePointerAssetUI_Impl;
    registry->assetUuidControl    = &UUIDForAssetUI_Impl;
#endif
}
