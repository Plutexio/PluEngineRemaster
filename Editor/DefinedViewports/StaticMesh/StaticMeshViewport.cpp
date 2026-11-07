//
// Created by Plutex on 1/13/26.
//

#include "StaticMeshViewport.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

#include "EditorAppContext.h"
#include "EngineAssets.h"
#include "StaticMeshDetailsPanel.h"
#include "StaticMeshViewportPanel.h"
#include "PluEngine/AssetTypes/Material/Material.h"
#include "Managers/Assets/EditorAssetManager.h"
#include "PluEngine/Gameplay/Components/PhysicsBodyComponent.h"

void Plu::EditorMeshObject::OnSetupComponents()
{
	MeshComponent = AddComponent(StaticMeshComponent::GetStaticClass(), "EditorMeshComponent");
	BodyComponent = AddComponent(PhysicsBodyComponent::GetStaticClass(), "BodyComponent");
}

void Plu::StaticMeshViewport::OnInit()
{
	// StaticMesh to asset binarny — domyślnie viewporty pozwalają zapisywać tylko JSON.
	// Włączamy zapis na żądanie (Ctrl+S), bo zmiany kolizji mają iść przez dirty + SaveAsset,
	// a nie zapisywać się natychmiast.
	SetCanBeSaved(true);
}

void Plu::StaticMeshViewport::OnClosed()
{
}

void Plu::StaticMeshViewport::OnOpened()
{
	AddPanel(StaticMeshDetailsPanel::GetStaticClass(), false);
	AddPanel(StaticMeshViewportPanel::GetStaticClass(), false);

	Material = mEditorAppContext->EditorAssetManager->GetAssetData(EngineAssets::StaticMeshViewportMaterial);
}

void Plu::StaticMeshViewport::OnPanelRegister()
{
	StaticMeshDetailsPanel* staticMeshDetailsPanel = GetPanelSlow<StaticMeshDetailsPanel>();
	StaticMeshViewportPanel* staticMeshViewportPanel = GetPanelSlow<StaticMeshViewportPanel>();
	if (staticMeshDetailsPanel) {
		ImGuiID dockspaceID = GetWindowDockID();

		ImGui::DockBuilderRemoveNode(dockspaceID);
		ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetWindowSize());

		ImGuiID right, left;
		ImGui::DockBuilderSplitNode(dockspaceID, ImGuiDir_Left, 0.7f, &left, &right);

		ImGui::DockBuilderDockWindow(staticMeshDetailsPanel->GetPanelTitle().CStr(), right);
		ImGui::DockBuilderDockWindow(staticMeshViewportPanel->GetPanelTitle().CStr(), left);
		ImGui::DockBuilderFinish(dockspaceID);
	}
}

void Plu::StaticMeshViewport::OnUpdate(float deltaTime)
{
	if (BeginWindow()) {
		UpdatePanels(deltaTime);
	}
	EndWindow();
}
