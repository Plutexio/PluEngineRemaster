//
// Created by Plutex on 1/14/26.
//

#include "SceneViewportPanel.h"
#include "PluEngine/FrameDemand.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

#include "Utils/GizmoUtils.h"
#include "EditorAppContext.h"
#include "EditorSettings/EditorSettings.h"
#include "EditorSettings/EditorSettingsManager.h"
#include "glm/gtc/type_ptr.hpp"
#include "Managers/Scene/EditorCamera.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"
#include "PluEngine/Gameplay/InputManager.h"
#include "PluEngine/Render/RenderingManager.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Render/GLFrameBuffer.h"
#include "PluEngine/Render/Renderer.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "PluEngine/Gameplay/Components/ParticleSpawnerComponent.h"
#include "PluEngine/Physics/JoltIntializer.h"
#include "PluEngine/Physics/PhysicsWorld.h"
#include "PluEngine/Platform/Window.h"

extern Plu::ApplicationInfo* gApplicationInfo;
extern Plu::EditorAppContext* gEditorAppContext;

Plu::String Plu::SceneViewportPanel::GetPanelName()
{
	return "Viewport";
}

void Plu::SceneViewportPanel::OnClosed()
{
}

void Plu::SceneViewportPanel::OnOpened()
{
	gApplicationInfo->AppScenesManager->UnloadOverlayScene();
}

void Plu::SceneViewportPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<SceneInfo> scene = gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
		if (scene)
		{
			if (!gApplicationInfo->AppScenesManager->IsInPIE()) JoltPhysics::GetPhysicsWorldBySceneHandle(gApplicationInfo->AppScenesManager->GetBaseSceneWorld()->GetObjectHandle())->OnUpdate(deltaTime, false);

			// Particles simulate on the render thread with every fresh snapshot, input or not.
			// Under power saving a visible viewport with a running effect has to ask for its frames.
			if (TUsePointer<SceneWorld> world = gApplicationInfo->AppScenesManager->GetCurrentWorld()) {
				for (const auto& spawner : world->GetParticleSpawnerComponents()) {
					if (spawner.second->IsPlaying() && !spawner.second->IsFinished()) {
						RequestContinuousRedraw();
						break;
					}
				}
			}

			ImVec2 viewportPos  = ImGui::GetCursorScreenPos();
			ImVec2 viewportSize = ImGui::GetContentRegionAvail();

			FrameBuffer* renderFBO = gApplicationInfo->AppRenderingManager->RequestMainFrameBuffer().GetRaw();

			float availW = viewportSize.x;
			float availH = viewportSize.y;

			float texAspect = (float)renderFBO->GetWidth() / (float)renderFBO->GetHeight();
			float availAspect = availW / availH;

			ImVec2 imageSize;

			// Dopasowanie bez zniekształcenia:
			if (availAspect > texAspect) {
				// Obszar UI jest bardziej poziomy → ograniczamy wysokość
				imageSize.y = availH;
				imageSize.x = imageSize.y * texAspect;
			} else {
				// Obszar UI jest bardziej pionowy → ograniczamy szerokość
				imageSize.x = availW;
				imageSize.y = imageSize.x / texAspect;
			}

			// Uzyskaj ID tekstury (ważne: to musi być zwykła tekstura, nie multisample!)
			GLuint texID = renderFBO->GetColorTexture()->GetID();

			// ImGui chce "ImTextureID"
			ImTextureID imguiTex = (ImTextureID)(intptr_t)texID;

			// Uwaga: OpenGL odwraca oś Y → dlatego UV są odwrotnie.
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			if (gEditorAppContext->EditorScenesManager->IsInPIE()) {
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.7f, 0.5f, 0.2f, 1.0f));
			} else {
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
			}
			ImVec2 posAfterTB = ImGui::GetCursorScreenPos();
			posAfterTB.y += ImGui::GetFontSize() + 2;

			//TaskBar
			ImGui::Text("Gizmo");
			ImGui::SameLine();
			TypeSerializer<GizmoOperation>::EditorControl(&gEditorAppContext->EditorState.CurrentGizmoOperation, "##Gizmo Operation");
			ImGui::SameLine();
			TypeSerializer<GizmoOperationSpace>::EditorControl(&gEditorAppContext->EditorState.CurrentGizmoOperationSpace, "##Operation Space");


			if (gApplicationInfo->AppInputManager->IsKeyDown(Key::Num1) && !gApplicationInfo->AppInputManager->IsKeyDown(Key::LeftShift)) {
				gEditorAppContext->EditorState.CurrentGizmoOperation = GizmoOperation::TRANSLATE;
			}
			if (gApplicationInfo->AppInputManager->IsKeyDown(Key::Num2) && !gApplicationInfo->AppInputManager->IsKeyDown(Key::LeftShift)) {
				gEditorAppContext->EditorState.CurrentGizmoOperation = GizmoOperation::ROTATE;
			}
			if (gApplicationInfo->AppInputManager->IsKeyDown(Key::Num3) && !gApplicationInfo->AppInputManager->IsKeyDown(Key::LeftShift)) {
				gEditorAppContext->EditorState.CurrentGizmoOperation = GizmoOperation::SCALE;
			}

			if (gApplicationInfo->AppInputManager->IsKeyDown(Key::Num1) && gApplicationInfo->AppInputManager->IsKeyDown(Key::LeftShift)) {
				gEditorAppContext->EditorState.CurrentGizmoOperationSpace = GizmoOperationSpace::LOCAL;
			}
			if (gApplicationInfo->AppInputManager->IsKeyDown(Key::Num2) && gApplicationInfo->AppInputManager->IsKeyDown(Key::LeftShift)) {
				gEditorAppContext->EditorState.CurrentGizmoOperationSpace = GizmoOperationSpace::WORLD;
			}

			ImGui::SetCursorScreenPos(posAfterTB);
			ImGui::BeginChild("Viewport", imageSize ,ImGuiChildFlags_Borders);
			ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::Image(imguiTex, imageSize, ImVec2(0,1), ImVec2(1,0));

			// Sterowanie kamerą tylko gdy kursor jest nad obrazem.
			if (ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + imageSize.x, pos.y + imageSize.y))) {
				if (gEditorAppContext->EditorScenesManager->GetEditorRenderCamera()) {
					gEditorAppContext->EditorSceneCamera->OnUpdate(deltaTime);
				}
			}

			// Gizmo — poza blokiem hovera, żeby przeciąganie nie urywało się gdy kursor zjedzie z obrazu.
			if (gEditorAppContext->EditorScenesManager->GetEditorRenderCamera() &&
				gApplicationInfo->AppObjectManager->IsValid(gEditorAppContext->EditorState.SelectedGameObject)) {
				TUsePointer<GameObject> selected = gApplicationInfo->AppObjectManager->GetObjectAsUser<GameObject>(gEditorAppContext->EditorState.SelectedGameObject);

				// Trzymaj macierze w lokalnych — GetObject*/GetLastFrame* zwracają przez wartość.
				Matrix4 view  = RenderSnapshotBuilder::GetLastFrameViewMatrix();
				Matrix4 proj  = RenderSnapshotBuilder::GetLastFrameProjectionMatrix();
				Matrix4 model = selected->GetObjectWorldMatrix();

				ImGuizmo::SetOrthographic(false);
				ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
				// Mapuj NDC->ekran na prostokąt obrazu (z letterboxingiem). Bez tego gizmo jest niewidoczne.
				ImGuizmo::SetRect(pos.x, pos.y, imageSize.x, imageSize.y);

				if (ImGuizmo::Manipulate(
						glm::value_ptr(view),
						glm::value_ptr(proj),
						PluGizmoOperationToImGuizmoOperation(gEditorAppContext->EditorState.CurrentGizmoOperation),
						PluGizmoModeToImGuizmoMode(gEditorAppContext->EditorState.CurrentGizmoOperationSpace),
						glm::value_ptr(model)) && ImGuizmo::IsUsing()) {
					// GameObject nie ma rodzica-GameObjectu => world == local; rotacja w stopniach (Euler).
					Vec3 translation(0.0f), rotation(0.0f), scale(1.0f);
					ImGuizmo::DecomposeMatrixToComponents(
						glm::value_ptr(model),
						glm::value_ptr(translation),
						glm::value_ptr(rotation),
						glm::value_ptr(scale));
					selected->SetObjectLocation(translation);
					selected->SetObjectRotation(rotation);
					selected->SetObjectScale(scale);
					PanelChangedAsset();
				}
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(4);
		}
	}
	EndPanel();}
