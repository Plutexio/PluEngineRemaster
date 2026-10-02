//
// Created by Plutex on 1/1/26.
//

#include "EngineStatsPanel.h"
#include "PluEngine/FrameDemand.h"

#include <iostream>

#include "EditorAppContext.h"
#include "TextureViewerPanel.h"
#include "Managers/Project/EditorProjectManager.h"
#include "Panels/EditorPanelManager.h"
#include "PluEngine/Application.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Render/Renderer.h"
#include "UI/IconsFontAwesome7.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"

Plu::String Plu::EngineStatsPanel::GetPanelName()
{
	return ICON_FA_GEARS " Engine Stats";
}

void Plu::EngineStatsPanel::OnUpdate(float deltaTime)
{
	static bool showDemoWindow = false;
	if (showDemoWindow) {
		ImGui::ShowDemoWindow(&showDemoWindow);
	}
	if (BeginPanel()) {
		// Live numbers, no input behind them: keep them ticking under power saving.
		RequestRedrawAfter(kStatsRefreshSeconds);
		//static String buildInfo = "Build Info: " + String(PLU_BUILD_TIME);
		//ImGui::Text("%s",buildInfo.CStr());
		ImGui::Checkbox("Demo Window", &showDemoWindow);
		if (ImGui::Button("Log executable path")) {
			PLU_ERROR("{}", GetExePath().ToString().ToNarrow().CStr());
		}
		if (ImGui::Button("Log User path")) {
			PLU_ERROR("{}", GetSystemUserPath().ToString().CStr());
		}
		static int numElements = 100;
		ImGui::DragInt("Num Elements to show", &numElements, 1, 0, mApplicationInfo->AppObjectManager->GetNumberOfObjects());
		DynamicArray<String> names = mApplicationInfo->AppObjectManager->GetObjectNames(numElements);
		for (UInt32 i = 0; i < names.Size(); i++) {
			String* obj = &names[i];
			if (ImGui::Selectable(obj->CStr())) {
				String clickInfo = "Click on ";
				clickInfo += *obj;
				PLU_TRACE(clickInfo.CStr());
			}
			if (ImGui::BeginPopupContextItem()) {
				if (!mApplicationInfo->AppObjectManager->GetObjectOnIndex(i)) {
					ImGui::Text("No Object at idx %d, nullptr there", i);
					ImGui::EndPopup();
					continue;
				}
				volatile int users = 0;
				volatile int owners = 0;
				{
					users = mApplicationInfo->AppObjectManager->GetObjectOnIndex(i).GetUseCount() - 1;
					owners = mApplicationInfo->AppObjectManager->GetObjectOnIndex(i).GetOwningCount();
				}
				ImGui::Text("Users - %d", users);
				ImGui::Text("Owners - %d", owners);
				// if (mApplicationInfo->AppObjectManager->GetObjectOnIndex(i)->GetClass() == IEditorAssetObject::GetStaticClass()) {
				// 	TUsePointer<IEditorAssetObject> assetObj = DynamicCast<IEditorAssetObject>(mApplicationInfo->AppObjectManager->GetObjectOnIndex(i));
				// 	ImGui::Text("Asset Name - %s", assetObj->GetAssetName().CStr());
				// 	ImGui::Text("Asset Type - %s", assetObj->GetAssetType().CStr());
				// }
				if (mApplicationInfo->AppObjectManager->GetObjectOnIndex(i)->GetClass()->IsDerivedOfOrSame(Texture::GetStaticClass())) {
					if (ImGui::MenuItem("View Texture")) {
						TUsePointer<TextureViewerPanel> viewer = mEditorPanelManager->AddPanel<TextureViewerPanel>();
						viewer->TextureToView = mApplicationInfo->AppObjectManager->GetObjectOnIndex(i);
					}
				} else if (mApplicationInfo->AppObjectManager->GetObjectOnIndex(i)->GetClass()->IsDerivedOfOrSame(FrameBuffer::GetStaticClass())) {
					if (ImGui::MenuItem("View Framebuffer")) {
						TUsePointer<TextureViewerPanel> viewer = mEditorPanelManager->AddPanel<TextureViewerPanel>();
						viewer->FrameBufferToView = mApplicationInfo->AppObjectManager->GetObjectOnIndex(i);
					}
				}
				if (ImGui::MenuItem("Serialize")) {
					nlohmann::json json;
					json = TypeSerializer<TypeInfo*>::Serialize(mApplicationInfo->AppObjectManager->GetObjectOnIndex(i)->GetClass(), mApplicationInfo->AppObjectManager->GetObjectOnIndex(i).GetRaw());
					String fileName = mApplicationInfo->AppObjectManager->GetObjectOnIndex(i)->GetDisplayName() + ".json";
					std::filesystem::create_directories((mEditorAppContext->EditorProjectManager->GetProjectCacheDirectory().ToString() + L"/SerializerTests/").CStr());
					DiskManager::SaveJson(mEditorAppContext->EditorProjectManager->GetProjectCacheDirectory().ToString() + L"/SerializerTests/" + fileName.ToWide(), json);
					PLU_WARN("Saved TestSerialize Test to: {}", (mEditorAppContext->EditorProjectManager->GetProjectCacheDirectory().ToString().ToNarrow() + "/SerializerTests/" + fileName).CStr());
				}
				ImGui::EndPopup();
			}
		}
	}
	EndPanel();
}

void Plu::EngineStatsPanel::OnHide()
{
}

void Plu::EngineStatsPanel::OnShow()
{
	SetCanClose(true);
}
