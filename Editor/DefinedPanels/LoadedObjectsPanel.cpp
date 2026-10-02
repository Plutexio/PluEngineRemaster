//
// Created by Plutex on 2026-06-26.
//

#include "LoadedObjectsPanel.h"
#include "PluEngine/FrameDemand.h"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "EditorAppContext.h"
#include "InputViewerPanel.h"
#include "TextureViewerPanel.h"
#include "Managers/Project/EditorProjectManager.h"
#include "Panels/EditorPanelManager.h"
#include "PluEngine/Application.h"
#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Render/GLTexture.h"
#include "PluEngine/Render/GLFrameBuffer.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/Render/Renderer.h"
#include "UI/IconsFontAwesome7.h"

Plu::String Plu::LoadedObjectsPanel::GetPanelName()
{
	return ICON_FA_CUBES " Loaded Objects";
}

void Plu::LoadedObjectsPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel()) {
		// Live numbers, no input behind them: keep them ticking under power saving.
		RequestRedrawAfter(kStatsRefreshSeconds);
		EngineObjectManager* objectManager = mApplicationInfo->AppObjectManager.GetRaw();
		if (!objectManager) {
			ImGui::TextDisabled("No object manager available.");
			EndPanel();
			return;
		}

		ImGui::Text("Live objects: %d", objectManager->GetNumberOfObjects());

		static int numElements = 200;
		ImGui::DragInt("Num elements to show", &numElements, 1, 0, objectManager->GetNumberOfObjects());

		static std::string filter;
		ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS " Filter by name", &filter);
		ImGui::Separator();

		DynamicArray<String> names = objectManager->GetObjectNames(numElements);
		for (UInt32 i = 0; i < names.Size(); i++) {
			String* name = &names[i];
			if (!filter.empty() && !name->Contains(filter.c_str())) {
				continue;
			}

			ImGui::PushID(static_cast<int>(i));
			ImGui::Selectable(name->CStr());

			if (ImGui::BeginPopupContextItem()) {
				TUsePointer<EngineObject> object = objectManager->GetObjectOnIndex(i);
				if (!object) {
					ImGui::Text("No object at idx %d (nullptr)", i);
					ImGui::EndPopup();
					ImGui::PopID();
					continue;
				}

				int users = objectManager->GetObjectOnIndex(i).GetUseCount() - 1;
				int owners = objectManager->GetObjectOnIndex(i).GetOwningCount();
				ImGui::Text("Class: %s", object->GetClass()->TypeName.CStr());
				ImGui::Text("Users: %d", users);
				ImGui::Text("Owners: %d", owners);
				ImGui::Separator();

				if (object->GetClass()->IsDerivedOfOrSame(Texture::GetStaticClass())) {
					if (ImGui::MenuItem(ICON_FA_IMAGE " View Texture")) {
						TUsePointer<TextureViewerPanel> viewer = mEditorPanelManager->AddPanel<TextureViewerPanel>();
						viewer->TextureToView = object;
					}
				} else if (object->GetClass()->IsDerivedOfOrSame(FrameBuffer::GetStaticClass())) {
					if (ImGui::MenuItem(ICON_FA_IMAGE " View Framebuffer")) {
						TUsePointer<TextureViewerPanel> viewer = mEditorPanelManager->AddPanel<TextureViewerPanel>();
						viewer->FrameBufferToView = object;
					}
				} else if (object->GetClass()->IsDerivedOfOrSame(GameObject::GetStaticClass())) {
					TUsePointer<GameObject> gameObject = object;
					if (gameObject->GetInputHandler()) {
						if (ImGui::MenuItem(ICON_FA_MAGNIFYING_GLASS " View Input Handler")) {
							TUsePointer<InputViewerPanel> viewer = mEditorPanelManager->AddPanel<InputViewerPanel>();
							viewer->InputHandlerOwner = gameObject;
						}
					}
				}

				if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK " Serialize")) {
					nlohmann::json json = TypeSerializer<TypeInfo*>::Serialize(object->GetClass(), object.GetRaw());
					String fileName = object->GetDisplayName() + ".json";
					auto outDir = mEditorAppContext->EditorProjectManager->GetProjectCacheDirectory().ToString() + L"/SerializerTests/";
					std::filesystem::create_directories(outDir.CStr());
					DiskManager::SaveJson(outDir + fileName.ToWide(), json);
					PLU_WARN("Serialized object to: {}", (outDir.ToNarrow() + fileName).CStr());
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
	}
	EndPanel();
}

void Plu::LoadedObjectsPanel::OnHide()
{
}

void Plu::LoadedObjectsPanel::OnShow()
{
	SetCanClose(true);
}
