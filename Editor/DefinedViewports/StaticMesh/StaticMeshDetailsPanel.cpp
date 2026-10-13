//
// Created by Plutex on 1/14/26.
//

#include "StaticMeshDetailsPanel.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

#include "StaticMeshViewport.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/Material/Material.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"
#include "PluEngine/Gameplay/Components/StaticMeshComponent.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "PluEngine/AssetPipeline/StaticMesh/StaticMeshAssimpLoader.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/Core/Widgets/Combo.h"
#include "PluEngine/Physics/JoltIntializer.h"
#include "PluEngine/Physics/PhysicsWorld.h"
#include "PluEngine/Physics/StaticMeshCollision.h"
#include "UI/IconsFontAwesome7.h"

extern Plu::ApplicationInfo* gApplicationInfo;
extern Plu::EditorAppContext* gEditorAppContext;

Plu::String Plu::StaticMeshDetailsPanel::GetPanelName()
{
	return "Properties";
}

void Plu::StaticMeshDetailsPanel::OnClosed()
{
}

void Plu::StaticMeshDetailsPanel::OnOpened()
{
}

void Plu::StaticMeshDetailsPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<StaticMeshViewport> parentMeshViewport = DynamicCast<StaticMeshViewport>(GetParentViewport());
		TUsePointer<MaterialInfo> before = parentMeshViewport->Material;
		TypeSerializer<TUsePointer<MaterialInfo>>::EditorControl(&parentMeshViewport->Material, "Material");
		if (before != parentMeshViewport->Material) {
			TUsePointer<EditorMeshObject> meshObject = gApplicationInfo->AppScenesManager->GetCurrentWorld()->GetGameObjectOfClass(EditorMeshObject::GetStaticClass());
			meshObject->MeshComponent->SetMaterial(parentMeshViewport->Material);
		}

		TUsePointer<StaticMesh> staticMesh = gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
		if (staticMesh)
		{
			ImGui::Text("Vertices: %lu", staticMesh->StaticMeshData.Vertices.Size());
			ImGui::Text("Triangles: %lu", staticMesh->StaticMeshData.Indices.Size() / 3);

			// Re-fit the camera to the mesh bounds (also runs automatically when the viewport opens).
			if (ImGui::Button(ICON_FA_CROSSHAIRS " Frame"))
				parentMeshViewport->NeedsFraming = true;

			ImGui::Separator();

			if (ImGui::CollapsingHeader("Collision"))
			{
				static DynamicArray<TypeInfo*> collisionTypeInfos;
				if (collisionTypeInfos.IsEmpty()) {
					for (auto type : *TypeRegistry::GetInstance()->GetTypeMap()) {
						if (type.second->Type != TypeType::STRUCT) continue;
						if (type.second->IsDerivedOf(IStaticMeshCollisionData::GetStaticClass())) {
							collisionTypeInfos.PushBack(type.second);
						}
					}
				}

				bool changed = false;
				TypeInfo* collisionType = staticMesh->CollisionData ? staticMesh->CollisionData->GetClass() : nullptr;
				if (ImGuiWidgets::ComboValue("Current Collision", &collisionType, collisionTypeInfos,
						[](TypeInfo* type) { return type->TypeName.CStr(); }, nullptr, 0, "No collision"))
				{
					staticMesh->CollisionName = collisionType->TypeName.CStr();
					PluUUID uuid = gApplicationInfo->AppScenesManager->GetCurrentWorld()->GetGameObjectOfClass(EditorMeshObject::GetStaticClass())->GetObjectUUID();
					JoltPhysics::GetPhysicsWorldBySceneHandle(gApplicationInfo->AppScenesManager->GetCurrentWorld()->GetObjectHandle())->RebuildObjectCollision(uuid);
					JoltPhysics::GetPhysicsWorldBySceneHandle(gApplicationInfo->AppScenesManager->GetBaseSceneWorld()->GetObjectHandle())->RebuildObjectsThatUseMesh(staticMesh.GetRaw());
					changed = true;
					PanelChangedAsset();
				}

				PhysicsWorld* physicsWorld = JoltPhysics::GetPhysicsWorldBySceneHandle(gApplicationInfo->AppScenesManager->GetCurrentWorld()->GetObjectHandle()).GetRaw();
				if (physicsWorld) {
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
			}
		}
	}
	EndPanel();
}
