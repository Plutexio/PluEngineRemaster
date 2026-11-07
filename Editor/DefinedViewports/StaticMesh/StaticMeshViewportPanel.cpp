//
// Created by Plutex on 2026-03-05.
//

#include "StaticMeshViewportPanel.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetTypes/MeshBounds.h"

#include <glad/glad.h>
#include "EditorAppContext.h"
#include "StaticMeshViewport.h"
#include "Managers/Scene/EditorCamera.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Render/GLFrameBuffer.h"
#include "PluEngine/Render/Renderer.h"
#include "PluEngine/AssetTypes/Material/Material.h"
#include "PluEngine/Gameplay/Components/StaticMeshComponent.h"
#include "PluEngine/Gameplay/InputManager.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"

#include "PluEngine/Render/RenderingManager.h"
#include "PluEngine/Core/BoundingBox.h"
#include "PluEngine/Physics/JoltIntializer.h"
#include "PluEngine/Physics/PhysicsWorld.h"

extern Plu::ApplicationInfo* gApplicationInfo;
extern Plu::EditorAppContext* gEditorAppContext;

Plu::String Plu::StaticMeshViewportPanel::GetPanelName()
{
	return "Viewport";
}

namespace
{
	// Fits the shared editor camera to the mesh's bounds (same helper the scene's "Fit In View"
	// and the skeletal mesh viewport use). Returns false while the mesh has no vertices yet, so
	// the caller can keep the framing request pending across async loads.
	bool FrameCameraToMesh(Plu::EditorMeshObject* meshObject, ImVec2 imageSize)
	{
		using namespace Plu;
		if (!meshObject || !meshObject->MeshComponent) return false;
		StaticMeshComponent* component = meshObject->MeshComponent.GetRaw();
		TUsePointer<StaticMesh> staticMesh = component->GetStaticMesh();
		if (!staticMesh || staticMesh->StaticMeshData.Vertices.IsEmpty()) return false;

		EditorSceneCamera* camera = gEditorAppContext->EditorSceneCamera.GetRaw();
		if (!camera) return false;

		BoundingBox box = CreateBoundingBoxForStaticMesh(staticMesh.GetRaw());
		const Vec3 newLoc = box.FitCamera(meshObject->GetObjectLocation(), camera->GetCameraRotation(),
		                                  Vec2(imageSize.x, imageSize.y), camera->GetCameraOptions()->FieldOfView);
		camera->SetCameraLocation(newLoc);
		return true;
	}
}

void Plu::StaticMeshViewportPanel::OnClosed()
{
}

void Plu::StaticMeshViewportPanel::OnOpened()
{
	PLU_INFO("Mesh Viewport Opened!");
	gEditorAppContext->EditorScenesManager->CreateOverlayScene();
	TUsePointer<SceneWorld> overlay = gEditorAppContext->EditorScenesManager->GetCurrentWorld();
	TUsePointer<EditorMeshObject> meshObject = overlay->SpawnGameObject(EditorMeshObject::GetStaticClass());
	TUsePointer<StaticMesh> staticMesh = gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
	meshObject->MeshComponent->SetStaticMesh(staticMesh);
	TUsePointer<StaticMeshViewport> parentMeshViewport = DynamicCast<StaticMeshViewport>(GetParentViewport());
	meshObject->MeshComponent->SetMaterial(parentMeshViewport->Material);
}

void Plu::StaticMeshViewportPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<StaticMeshViewport> parentMeshViewport = DynamicCast<StaticMeshViewport>(GetParentViewport());

		// Wizualizacja kolizji jedzie centralną ścieżką debug-geometrii: RenderSnapshotBuilder
		// na MAIN woła DrawEditModeShapes() na PhysicsWorld bieżącego świata (= overlay tego
		// viewportu, bo GetCurrentWorld() zwraca overlay), pakuje linie do snapshotu, a wątek
		// renderu je rysuje. Tu tylko sterujemy trybem z checkboxa i invalidujemy cache kształtów.
		TUsePointer<SceneWorld> overlay = gEditorAppContext->EditorScenesManager->GetCurrentWorld();
		// PhysicsWorld* overlayPhysics = overlay ? overlay->GetPhysicsWorld() : nullptr;
		// if (overlayPhysics)
		// {
		// 	// overlayPhysics->PhysicsDebugRenderMode =
		// 	// 	parentMeshViewport->ShowCollision ? PhysicsDebugRender::WIREFRAME : PhysicsDebugRender::NONE; TODO
		// }
		JoltPhysics::GetPhysicsWorldBySceneHandle(gApplicationInfo->AppScenesManager->GetCurrentWorld()->GetObjectHandle())->OnUpdate(deltaTime, false);

		if (parentMeshViewport->CollisionDirty)
		{
			TUsePointer<StaticMesh> staticMesh = gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
			// if (overlayPhysics)
			// 	overlayPhysics->InvalidateMeshCollisionCache(staticMesh.GetRaw()); TODO
			parentMeshViewport->CollisionDirty = false;
		}

		FrameBuffer* renderFBO = gApplicationInfo->AppRenderingManager->RequestMainFrameBuffer().GetRaw();

		ImVec2 viewportSize = ImGui::GetContentRegionAvail();

		float availW = viewportSize.x;
		float availH = viewportSize.y;

		float texAspect = (float)renderFBO->GetWidth() / (float)renderFBO->GetHeight();
		float availAspect = availW / availH;

		ImVec2 imageSize;

		if (availAspect > texAspect) {
			imageSize.y = availH;
			imageSize.x = imageSize.y * texAspect;
		} else {
			imageSize.x = availW;
			imageSize.y = imageSize.x / texAspect;
		}

		GLuint texID = renderFBO->GetColorTexture()->GetID();
		ImTextureID imguiTex = (ImTextureID)(intptr_t)texID;

		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::Image(imguiTex, imageSize, ImVec2(0,1), ImVec2(1,0));

		bool hovered = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + imageSize.x, pos.y + imageSize.y));

		// Fit the camera to the mesh on request; keep it pending until the mesh has vertices.
		if (parentMeshViewport && parentMeshViewport->NeedsFraming)
		{
			TUsePointer<EditorMeshObject> meshObject = overlay
				? DynamicCast<EditorMeshObject>(overlay->GetGameObjectOfClass(EditorMeshObject::GetStaticClass()))
				: nullptr;
			if (FrameCameraToMesh(meshObject.GetRaw(), imageSize))
				parentMeshViewport->NeedsFraming = false;
		}
		// Kamera stoi w PIE: PIE kasuje overlay tego viewportu i renderuje grę do tego samego FBO,
		// więc nie ma tu czego oglądać ani czym ruszać. Patrz notatka o PIE w Editor/CLAUDE.md.
		if (hovered) {
			if (!gEditorAppContext->EditorScenesManager->IsInPIE()) {
				if (gEditorAppContext->EditorSceneCamera) {
					DynamicCast<EditorSceneCamera>(gEditorAppContext->EditorSceneCamera)->OnUpdate(deltaTime);
				}
			}
		}
	}
	EndPanel();
}
