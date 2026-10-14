//
// Created by Plutex on 10/14/26.
//

#include "BlendSpaceViewport.h"

#include "BlendSpaceCanvasPanel.h"
#include "BlendSpaceDetailsPanel.h"
#include "BlendSpacePreviewPanel.h"
#include "EditorAppContext.h"
#include "EngineAssets.h"
#include "Managers/Assets/EditorAssetManager.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetTypes/Material/Material.h"
#include "PluEngine/AssetTypes/AnimationGraph/Nodes/AnimBlendSpaceNode.h"
#include "PluEngine/AssetTypes/AnimationGraph/Nodes/AnimOutputPoseNode.h"
#include "PluEngine/AssetTypes/AnimationGraph/Nodes/AnimVariableNode.h"

extern Plu::ApplicationInfo* gApplicationInfo;

void Plu::EditorBlendSpacePreviewObject::OnSetupComponents()
{
	MeshComponent = AddComponent(SkeletalMeshComponent::GetStaticClass(), "BlendSpacePreviewMeshComponent");
}

Plu::TUsePointer<Plu::BlendSpace> Plu::BlendSpaceViewport::GetBlendSpace()
{
	return gApplicationInfo->AppAssetManager->GetAssetData(GetAssetDescriptor());
}

void Plu::BlendSpaceViewport::BuildPreviewGraph()
{
	mPreviewGraph = CreateOwning<AnimationGraph>();
	AnimationGraph& graph = *mPreviewGraph;

	auto* blendNode = static_cast<AnimBlendSpaceNode*>(graph.AddNode(AnimBlendSpaceNode::GetStaticClass()));
	auto* outputNode = graph.AddNode(AnimOutputPoseNode::GetStaticClass());
	if (!blendNode || !outputNode) return;
	blendNode->BlendSpaceAsset = GetBlendSpace();
	mPreviewNodeUuid = blendNode->Uuid;

	// The canvas' preview cursor reaches the node through real graph variables (not the node's
	// fallback X/Y): setting a variable bumps the instance's value revision, which is what
	// invalidates RenderSnapshotBuilder's pose cache while the preview is paused.
	for (const char* name : { "X", "Y" }) {
		TOwningPointer<IAnimationGraphVariable> variable = AnimationGraphVariableFactory::CreateVariable("Float");
		if (!variable) continue;
		variable->Name = name;
		graph.Variables.PushBack(variable);
		AnimVariableNode* variableNode = graph.AddVariableNode(graph.FindVariable(name));
		if (variableNode) (String(name) == "X" ? mPreviewXNodeUuid : mPreviewYNodeUuid) = variableNode->Uuid;
	}

	graph.RebuildAllPins();
	graph.Connect(blendNode->Uuid, "Pose", outputNode->Uuid, "Pose");
	graph.Connect(mPreviewXNodeUuid, "Value", blendNode->Uuid, "X");
	graph.Connect(mPreviewYNodeUuid, "Value", blendNode->Uuid, "Y"); // fails harmlessly for 1D
}

void Plu::BlendSpaceViewport::SyncPreviewGraph()
{
	if (!mPreviewGraph) return;
	GraphNode* node = mPreviewGraph->FindNode(mPreviewNodeUuid);
	if (!node || !node->ArePinsStale()) return;

	mPreviewGraph->RebuildAllPins();
	mPreviewGraph->PruneInvalidLinks();
	// Re-wire Y after a 1D -> 2D switch (Connect refuses duplicates and missing pins).
	mPreviewGraph->Connect(mPreviewXNodeUuid, "Value", mPreviewNodeUuid, "X");
	mPreviewGraph->Connect(mPreviewYNodeUuid, "Value", mPreviewNodeUuid, "Y");
}

void Plu::BlendSpaceViewport::OnOpened()
{
	// Preview first so it takes the large slot; canvas under it, details on the right.
	AddPanel(BlendSpacePreviewPanel::GetStaticClass(), false);
	AddPanel(BlendSpaceCanvasPanel::GetStaticClass(), false);
	AddPanel(BlendSpaceDetailsPanel::GetStaticClass(), false);

	// Same material SkeletalMeshViewport previews with (a skeletal program reading the bone SSBO).
	Material = mEditorAppContext->EditorAssetManager->GetAssetData(EngineAssets::SkeletalMeshViewportMaterial);

	BuildPreviewGraph();

	// Start the cursor where the space starts (e.g. Idle at Speed 0).
	if (TUsePointer<BlendSpace> space = GetBlendSpace())
		PreviewInput = space->ClampInput(Vec2(space->AxisX.Min, space->AxisY.Min));
}

void Plu::BlendSpaceViewport::OnClosed()
{
}

void Plu::BlendSpaceViewport::OnPanelRegister()
{
	BlendSpacePreviewPanel* previewPanel = GetPanelSlow<BlendSpacePreviewPanel>();
	BlendSpaceCanvasPanel* canvasPanel = GetPanelSlow<BlendSpaceCanvasPanel>();
	BlendSpaceDetailsPanel* detailsPanel = GetPanelSlow<BlendSpaceDetailsPanel>();
	if (previewPanel && canvasPanel && detailsPanel) {
		ImGuiID dockspaceID = GetWindowDockID();

		ImGui::DockBuilderRemoveNode(dockspaceID);
		ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetWindowSize());

		// Preview (top-left) over the sample canvas (bottom-left), details in a right column.
		ImGuiID left, right;
		ImGui::DockBuilderSplitNode(dockspaceID, ImGuiDir_Right, 0.28f, &right, &left);
		ImGuiID leftTop, leftBottom;
		ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.4f, &leftBottom, &leftTop);

		ImGui::DockBuilderDockWindow(previewPanel->GetPanelTitle().CStr(), leftTop);
		ImGui::DockBuilderDockWindow(canvasPanel->GetPanelTitle().CStr(), leftBottom);
		ImGui::DockBuilderDockWindow(detailsPanel->GetPanelTitle().CStr(), right);
		ImGui::DockBuilderFinish(dockspaceID);
	}
}

void Plu::BlendSpaceViewport::OnUpdate(float deltaTime)
{
	if (BeginWindow()) {
		UpdatePanels(deltaTime);
	}
	EndWindow();
}
