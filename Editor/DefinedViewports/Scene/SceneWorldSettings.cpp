//
// Created by Plutex on 2026-02-15.
//

#include "SceneWorldSettings.h"

#include "EditorAppContext.h"
#include "glm/gtc/type_ptr.hpp"
#include "Managers/Scene/EditorCamera.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Render/Renderer.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "PluEngine/Physics/JoltIntializer.h"
#include "PluEngine/Physics/PhysicsWorld.h"

extern Plu::ApplicationInfo* gApplicationInfo;
extern Plu::EditorAppContext* gEditorAppContext;

Plu::String Plu::SceneWorldSettings::GetPanelName()
{
	return "World Settings";
}

void Plu::SceneWorldSettings::OnClosed()
{
}

void Plu::SceneWorldSettings::OnOpened()
{
}

void Plu::SceneWorldSettings::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<SceneInfo> scene = gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
		if (scene && gEditorAppContext->EditorScenesManager->IsAnySceneOpen()) {
			if (TypeSerializer<TClassPointer<GameMode>>::EditorControl(
				&gEditorAppContext->EditorScenesManager->GetCurrentWorld()->GameModeClass,
				"GameModeClass")) {
				PanelChangedAsset();
			}
		}

		ImGui::Separator();
		ImGui::Text("World Stats");
		PhysicsWorld* physicsWorld = JoltPhysics::GetPhysicsWorldBySceneHandle(gEditorAppContext->EditorScenesManager->GetCurrentWorld()->GetObjectHandle()).GetRaw();
		if (physicsWorld) {
			ImGui::Text("Physics Bodies: %d", physicsWorld->GetNumOfBodies());
			if (TypeSerializer<PhysicsDebugRenderMode>::EditorControl(
				&physicsWorld->DebugRenderMode,
				"Physics Visualize Mode")) {
				}
			if (ImGui::ColorEdit3("Wireframe Color", &physicsWorld->DebugLineColor.x)) {
				PanelChangedAsset();
			}
			if (ImGui::ColorEdit3("Points Color", &physicsWorld->DebugPointColor.x)) {
				PanelChangedAsset();
			}
		}

		ImGui::Separator();
		// Editor grid — view-only state (like the viewport camera), deliberately not dirtying
		// the asset. Read on main by RenderSnapshotBuilder; hidden automatically during PIE.
		TUsePointer<SceneWorld> world = gEditorAppContext->EditorScenesManager->GetCurrentWorld();
		ImGui::Checkbox("Show Editor Grid", &world->ShowEditorGrid);
		ImGui::Checkbox("Show Shadow Cascades", &world->ShowShadowCascades);
		ImGui::SetItemTooltip("Tints each pixel by the shadow cascade it samples. The gradients between bands are the cascade blend zones.");
	}
	EndPanel();
}
