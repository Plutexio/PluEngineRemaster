//
// Created by Plutex on 2026-06-26.
//

#include "LoadedAssetsPanel.h"
#include "PluEngine/FrameDemand.h"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/Core/IAssetData.h"
#include "PluEngine/Core/Reflection/ReflectionBase.h"
#include "UI/IconsFontAwesome7.h"

namespace
{
	const char* LoaderTypeToString(Plu::AssetLoaderType type)
	{
		switch (type) {
			case Plu::AssetLoaderType::JSON:   return "JSON";
			case Plu::AssetLoaderType::Binary: return "Binary";
			default:                           return "Undefined";
		}
	}
}

Plu::String Plu::LoadedAssetsPanel::GetPanelName()
{
	return ICON_FA_DATABASE " Loaded Assets";
}

void Plu::LoadedAssetsPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel()) {
		// Live numbers, no input behind them: keep them ticking under power saving.
		RequestRedrawAfter(kStatsRefreshSeconds);
		if (!mApplicationInfo->AppAssetManager) {
			ImGui::TextDisabled("No asset manager available.");
			EndPanel();
			return;
		}

		// All reflected types that are assets (derive from IAssetData). Cached after first build.
		static DynamicArray<TypeInfo*> assetTypes;
		if (assetTypes.IsEmpty()) {
			for (auto& type : *TypeRegistry::GetInstance()->GetTypeMap()) {
				if (type.second != IAssetData::GetStaticClass() &&
				    type.second->IsDerivedOf(IAssetData::GetStaticClass())) {
					assetTypes.PushBack(type.second);
				}
			}
		}

		static std::string filter;
		ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS " Filter by name/path", &filter);
		ImGui::Separator();

		for (TypeInfo* type : assetTypes) {
			DynamicArray<TUsePointer<AssetDescriptor>> descriptors =
				mApplicationInfo->AppAssetManager->GetAllAssetDescriptorsOfType(type);
			if (descriptors.IsEmpty()) {
				continue;
			}

			String header = type->TypeName + " (" + String::FromInt(static_cast<UInt64>(descriptors.Size())) + ")";
			if (!ImGui::CollapsingHeader(header.CStr())) {
				continue;
			}

			ImGui::PushID(type->TypeName.CStr());
			ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
			                             ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
			if (ImGui::BeginTable("##assets", 4, tableFlags)) {
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Loader", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Loaded", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableHeadersRow();

				for (UInt32 i = 0; i < descriptors.Size(); i++) {
					TUsePointer<AssetDescriptor> desc = descriptors[i];
					if (!desc) {
						continue;
					}

					String path = desc->AssetPath.ToString();
					if (!filter.empty() &&
					    !desc->AssetName.Contains(filter.c_str()) && !path.Contains(filter.c_str())) {
						continue;
					}

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("%s", desc->AssetName.CStr());
					ImGui::TableNextColumn();
					ImGui::TextWrapped("%s", path.CStr());
					ImGui::TableNextColumn();
					ImGui::Text("%s", LoaderTypeToString(desc->LoaderType));
					ImGui::TableNextColumn();
					bool loaded = mApplicationInfo->AppAssetManager->IsAssetLoaded(desc->Uuid);
					ImGui::TextColored(loaded ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
					                   "%s", loaded ? "yes" : "no");
				}
				ImGui::EndTable();
			}
			ImGui::PopID();
		}
	}
	EndPanel();
}

void Plu::LoadedAssetsPanel::OnHide()
{
}

void Plu::LoadedAssetsPanel::OnShow()
{
	SetCanClose(true);
}
