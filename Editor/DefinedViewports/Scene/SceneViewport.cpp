//
// Created by Plutex on 1/13/26.
//

#include "SceneViewport.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

#include "EditorAppContext.h"
#include "SceneObjectDetailsPanel.h"
#include "SceneStructurePanel.h"
#include "SceneViewportPanel.h"
#include "SceneWorldSettings.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "PluEngine/Gameplay/Objects/Lights/SpotLight.h"
#include "PluEngine/Render/RenderUtils.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Effects/Particles/ParticleSystem.h"
#include "PluEngine/Effects/Particles/ParticleSystemCompiler.h"
#include <glm/gtc/quaternion.hpp>

extern Plu::EditorAppContext* gEditorAppContext;
extern Plu::TUsePointer<Plu::EngineObjectManager> gEngineObjectManager;

void Plu::SceneViewport::SubscribeToCurrentWorld()
{
	TUsePointer<SceneWorld> world = mEditorAppContext->EditorScenesManager->GetBaseSceneWorld();
	if (!world) return;
	if (mSubscribedWorld && *mSubscribedWorld->GetEngineObjectHandle() == *world->GetEngineObjectHandle()) return;
	UnsubscribeFromWorld();
	mSubscribedWorld = world;
	mGameObjectsChangedHandle = world->GetObjectEventDispatcher()->Subscribe("GameObjectsChanged", [this](void*) {
		if (mSuppressDirtyFromWorld) return;
		ViewportChangedAsset();
	});
}

void Plu::SceneViewport::UnsubscribeFromWorld()
{
	if (mSubscribedWorld && mGameObjectsChangedHandle != 0) {
		mSubscribedWorld->GetObjectEventDispatcher()->Unsubscribe("GameObjectsChanged", mGameObjectsChangedHandle);
	}
	mSubscribedWorld          = nullptr;
	mGameObjectsChangedHandle = 0;
}

void Plu::SceneViewport::OnInit()
{
	// Opening another scene reuses this viewport (EditorViewportManager::CreateViewport calls
	// Initialize again), and mAsset already points at the new scene here. Unloading the old world
	// destroys all of its objects, which dispatches "GameObjectsChanged" — with the subscription
	// still alive that marked the just-opened scene dirty before the user touched anything.
	UnsubscribeFromWorld();
	TUsePointer<SceneInfo> scene = gEditorAppContext->EditorAssetManager->GetAssetData(GetAssetDescriptor());
	mEditorAppContext->EditorScenesManager->ConnectToWorld(scene->URL, false);
	// After the load: the spawns that build the scene from JSON are not user edits either. This also
	// re-points the subscription at the new world — without it the reused viewport would keep
	// listening to the destroyed one and never dirty the scene again.
	SubscribeToCurrentWorld();
}

void Plu::SceneViewport::OnClosed()
{
	UnsubscribeFromWorld();
	// OnOpened runs on every open, so without this a reopened viewport would stack handlers and each
	// reloaded class would be processed once per stacked subscription.
	TypeRegistry::GetInstance()->TypeRegistryEventDispatcher.Unsubscribe("NewPythonType", mNewPythonTypeHandle);
	mPendingPythonTypeReloads.Clear();
	gEditorAppContext->EditorScenesManager->DisconnectFromWorld();
	PLU_CORE_INFO("Scene Viewport closed!");
}

void Plu::SceneViewport::OnOpened()
{
	AddPanel(SceneStructurePanel::GetStaticClass(), false);
	AddPanel(SceneViewportPanel::GetStaticClass(), false);
	AddPanel(SceneInspectorPanel::GetStaticClass(), false);
	AddPanel(SceneWorldSettings::GetStaticClass(), false);

	// Normally already done by OnInit — this only covers a reopen where the world changed in between.
	SubscribeToCurrentWorld();

	mNewWorldHandle = mEditorAppContext->EditorScenesManager->GetObjectEventDispatcher()->Subscribe("NewWorld", [](void*) {
		gEditorAppContext->EditorState.SelectedGameObject = EngineObjectHandle();
		gEditorAppContext->EditorState.SelectedGameObjectComponent = EngineObjectHandle();
	});

	// Only queued here — see mPendingPythonTypeReloads for why the recreate pass cannot run from
	// inside the dispatch.
	mNewPythonTypeHandle = TypeRegistry::GetInstance()->TypeRegistryEventDispatcher.Subscribe("NewPythonType", [this](void* data) {
		String* typeName = static_cast<String *>(data);
		if (!typeName || typeName->IsEmpty()) return;
		if (mPendingPythonTypeReloads.Contains(*typeName)) return;
		mPendingPythonTypeReloads.PushBack(*typeName);
	});
}

void Plu::SceneViewport::FlushPendingPythonTypeReloads()
{
	if (mPendingPythonTypeReloads.IsEmpty()) return;
	DynamicArray<String> typeNames = std::move(mPendingPythonTypeReloads);
	mPendingPythonTypeReloads.Clear();

	TUsePointer<SceneManager> scenesManager = gEditorAppContext->EditorScenesManager;
	if (!scenesManager || !scenesManager->IsAnySceneOpen()) return;
	if (scenesManager->IsInPIE()) {
		PLU_WARN("Python scripts reloaded, but live objects were left alone — stop PIE and reload again to apply the new code to them.");
		return;
	}

	// The recreated object is a different EngineObject, so the stored handle stops resolving. UUID for
	// the object, name for the component — the two keys that survive a recreate by design.
	UInt64 selectedUuid = 0;
	String selectedComponentName;
	if (gEngineObjectManager->IsValid(gEditorAppContext->EditorState.SelectedGameObject)) {
		if (TUsePointer<GameObject> selected = gEngineObjectManager->GetObjectAsUser<GameObject>(gEditorAppContext->EditorState.SelectedGameObject)) {
			selectedUuid = selected->GetObjectUUID();
			if (gEngineObjectManager->IsValid(gEditorAppContext->EditorState.SelectedGameObjectComponent)) {
				if (TUsePointer<GameObjectComponent> selectedComponent = gEngineObjectManager->GetObjectAsUser<GameObjectComponent>(gEditorAppContext->EditorState.SelectedGameObjectComponent)) {
					selectedComponentName = selectedComponent->GetComponentName();
				}
			}
		}
	}

	// The recreate destroys and respawns objects, which dispatches "GameObjectsChanged" — but every
	// object comes back from its own serialized state, so nothing the scene file holds has changed.
	// Left unguarded, a script reload marked the scene dirty on its own, without a single user edit.
	mSuppressDirtyFromWorld = true;
	scenesManager->ReloadPythonInstances(typeNames);
	mSuppressDirtyFromWorld = false;

	if (selectedUuid == 0) return;
	TUsePointer<SceneWorld> world = scenesManager->GetBaseSceneWorld();
	TUsePointer<GameObject> restored = world ? world->GetGameObjectByUUID(PluUUID(selectedUuid)) : nullptr;
	if (!restored) {
		// The object was python and its class failed to come back (script error) — the details panel
		// must not keep editing a corpse.
		gEditorAppContext->EditorState.SelectedGameObject = EngineObjectHandle();
		gEditorAppContext->EditorState.SelectedGameObjectComponent = EngineObjectHandle();
		return;
	}
	gEditorAppContext->EditorState.SelectedGameObject = *restored->GetEngineObjectHandle();
	gEditorAppContext->EditorState.SelectedGameObjectComponent = EngineObjectHandle();
	if (selectedComponentName.IsEmpty()) return;
	for (const auto& worldComponent : *restored->GetObjectWorldComponents()) {
		if (!worldComponent || worldComponent->GetComponentName() != selectedComponentName) continue;
		gEditorAppContext->EditorState.SelectedGameObjectComponent = *worldComponent->GetEngineObjectHandle();
		return;
	}
	for (const auto& component : *restored->GetObjectComponents()) {
		if (!component || component->GetComponentName() != selectedComponentName) continue;
		gEditorAppContext->EditorState.SelectedGameObjectComponent = *component->GetEngineObjectHandle();
		return;
	}
}

void Plu::SceneViewport::OnPanelRegister()
{
	SceneStructurePanel* sceneDetailsPanel = GetPanelSlow<SceneStructurePanel>();
	SceneViewportPanel* sceneViewport = GetPanelSlow<SceneViewportPanel>();
	SceneInspectorPanel* sceneInspector = GetPanelSlow<SceneInspectorPanel>();
	SceneWorldSettings* sceneWorldSettings = GetPanelSlow<SceneWorldSettings>();
	if (sceneDetailsPanel && sceneViewport && sceneInspector && sceneWorldSettings)
	{
		ImGuiID dockspaceID = GetWindowDockID();

		ImGui::DockBuilderRemoveNode(dockspaceID);
		ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceID, GetDockspaceSize());

		ImGuiID left, right;
		ImGuiID rightDown, rightUp;
		ImGui::DockBuilderSplitNode(dockspaceID, ImGuiDir_Left, 0.8f, &left, &right);
		ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.5f, &rightUp, &rightDown);
		ImGui::DockBuilderDockWindow(sceneDetailsPanel->GetPanelTitle().CStr(), rightUp);
		ImGui::DockBuilderDockWindow(sceneInspector->GetPanelTitle().CStr(), rightDown);
		ImGui::DockBuilderDockWindow(sceneWorldSettings->GetPanelTitle().CStr(), rightDown);
		ImGui::DockBuilderDockWindow(sceneViewport->GetPanelTitle().CStr(), left);
		ImGui::DockBuilderFinish(dockspaceID);

		// Inspector and World Settings share rightDown as tabs. Focus the Inspector on its next Begin
		// so it's the active tab on open instead of hiding behind World Settings.
		sceneInspector->BringToFront();
	}
}

void Plu::SceneViewport::DrawSelectedSpotLightGizmo()
{
	if (!gEngineObjectManager->IsValid(gEditorAppContext->EditorState.SelectedGameObject)) return;

	TUsePointer<GameObject> selected = gEngineObjectManager->GetObjectAsUser<GameObject>(gEditorAppContext->EditorState.SelectedGameObject);
	if (!selected || !selected->GetClass()->IsDerivedOfOrSame(SpotLight::GetStaticClass())) return;

	TUsePointer<SceneWorld> world = gEditorAppContext->EditorScenesManager->GetCurrentWorld();
	if (!world) return;

	SpotLight* spotLight = static_cast<SpotLight*>(selected.GetRaw());
	const Vec3 apex      = spotLight->GetObjectLocation();
	const Vec3 direction = spotLight->GetObjectForwardVector();

	// Two cones, so the falloff band is visible as a band rather than guessed from one number:
	// the outer one is where the light reaches zero, the inner one where it is still at full
	// brightness. Tinted with the light's own colour so several selected lamps stay tellable
	// apart; the inner cone is dimmed to read as the "inside".
	const Vec3 outerColor = spotLight->GetLightColor();
	const Vec3 innerColor = outerColor * 0.45f;

	// Drawn through the world's debug draw adapter; RenderSnapshotBuilder drains it into the
	// snapshot and the existing debug-line pass draws it — no new renderer code. Clamped like the
	// light itself (RenderSnapshotBuilder), so the gizmo shows the cone that is actually lit.
	const float outerAngle = glm::clamp(spotLight->OuterConeAngle, 0.5f, 89.0f);
	DebugDrawAdapter* debugDraw = world->GetDebugDraw();
	debugDraw->DrawDebugCone(apex, direction, spotLight->Range, outerAngle, outerColor);
	debugDraw->DrawDebugCone(apex, direction, spotLight->Range, glm::min(spotLight->InnerConeAngle, outerAngle), innerColor);
}

void Plu::SceneViewport::DrawSelectedParticleSpawnerGizmos()
{
	if (!gEngineObjectManager->IsValid(gEditorAppContext->EditorState.SelectedGameObject)) return;

	TUsePointer<GameObject> selected = gEngineObjectManager->GetObjectAsUser<GameObject>(gEditorAppContext->EditorState.SelectedGameObject);
	if (!selected) return;

	TUsePointer<SceneWorld> world = gEditorAppContext->EditorScenesManager->GetCurrentWorld();
	if (!world) return;

	// Fixed length: the gizmo shows direction and spread, not how far particles fly.
	constexpr float kGizmoLength = 1.0f;
	constexpr float kAtRestMarkerRadius = 0.1f;
	const Vec3 coneColor = Vec3(1.0f, 0.55f, 0.1f);
	const Vec3 axisColor = Vec3(1.0f, 0.85f, 0.3f);
	DebugDrawAdapter& debugDraw = *world->GetDebugDraw();
}

void Plu::SceneViewport::OnUpdate(float deltaTime)
{
	// Outside BeginWindow: the scripts are reloaded from a menu item, and the objects have to be
	// recreated whether or not this window happens to be visible this frame.
	FlushPendingPythonTypeReloads();
	DrawSelectedSpotLightGizmo();
	DrawSelectedParticleSpawnerGizmos();
	if (BeginWindow()) {
		if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
			TUsePointer<SceneInfo> scene = gEditorAppContext->EditorAssetManager->GetAssetData(GetAssetDescriptor());
			if (scene && gEditorAppContext->EditorScenesManager->IsAnySceneOpen()) {
				if (gEngineObjectManager->IsValid(gEditorAppContext->EditorState.SelectedGameObjectComponent) &&
					gEngineObjectManager->IsValid(gEditorAppContext->EditorState.SelectedGameObject)) {
					TUsePointer<GameObject> selected = gEngineObjectManager->GetObjectAsUser<GameObject>(gEditorAppContext->EditorState.SelectedGameObject);
					selected->DeleteComponent(gEngineObjectManager->GetObjectAsUser<GameObjectComponent>(gEditorAppContext->EditorState.SelectedGameObjectComponent).GetRaw());
					gEditorAppContext->EditorState.SelectedGameObjectComponent = EngineObjectHandle();
				} else if (gEngineObjectManager->IsValid(gEditorAppContext->EditorState.SelectedGameObject)) {
					// Multi-selection żyje w Structure panelu (EditorState trzyma tylko primary),
					// więc kasowanie oddajemy jemu. GetPanelSlow leci tylko z wciśniętym Delete.
					if (SceneStructurePanel* structurePanel = GetPanelSlow<SceneStructurePanel>()) {
						structurePanel->DeleteSelectedObjects();
					} else {
						TUsePointer<GameObject> gameObj = gEngineObjectManager->GetObjectAsUser<GameObject>(gEditorAppContext->EditorState.SelectedGameObject);
						gEditorAppContext->EditorScenesManager->GetCurrentWorld()->DeleteGameObject(*gameObj->GetEngineObjectHandle());
						gEditorAppContext->EditorState.SelectedGameObject = EngineObjectHandle();
						gEditorAppContext->EditorState.SelectedGameObjectComponent = EngineObjectHandle();
					}
				}
			}
		}
		UpdatePanels(deltaTime);
	}
	EndWindow();
}
