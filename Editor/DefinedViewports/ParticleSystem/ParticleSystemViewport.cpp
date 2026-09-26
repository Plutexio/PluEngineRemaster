//
// Created by Plutex on 9/26/26.
//

#include "ParticleSystemViewport.h"

#include "ParticleEmittersPanel.h"
#include "ParticleGraphPanel.h"
#include "ParticleParametersPanel.h"
#include "ParticleDetailsPanel.h"
#include "ParticlePreviewPanel.h"
#include "ParticleModuleNodeView.h"
#include "ParticleCurveParamNodeView.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/Effects/Particles/ParticleSystem.h"
#include "PluEngine/Effects/Particles/ParticleSystemInstance.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleSpawnModules.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleUpdateModules.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleRendererNodes.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleEmitterOutputNode.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleSourceNodes.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleParameterNode.h"

extern Plu::ApplicationInfo* gApplicationInfo;

namespace Plu
{
	// One shared, stateless view for every particle module type.
	static ParticleModuleNodeView sParticleModuleNodeView;
	// Same frame plus a curve / gradient thumbnail, for the over-life / by-speed modules.
	static ParticleCurveParamNodeView sParticleCurveParamNodeView;
}

Plu::StringW Plu::ParticleSystemViewport::GetLayoutPath()
{
	// Editor-owned layout sidecar next to the asset (node positions never enter the .pluasset).
	return (GetAssetDescriptor()->AssetPath.ToString() + ".layout.json").ToWide();
}

void Plu::ParticleSystemViewport::OnClosed()
{
	mNodeGraphEditor.SaveLayout(GetLayoutPath());
	for (auto& entry : mEmitterContexts) ImGuiNodeEditor::DestroyEditor(entry.second);
	mEmitterContexts.Clear();
}

void Plu::ParticleSystemViewport::OnOpened()
{
	// The view registry matches on the exact concrete type name.
	TypeInfo* moduleTypes[] = {
		SpawnRateModule::GetStaticClass(), SpawnBurstModule::GetStaticClass(), InitLifetimeModule::GetStaticClass(),
		InitLocationModule::GetStaticClass(), InitVelocityModule::GetStaticClass(), InitSizeModule::GetStaticClass(),
		InitColorModule::GetStaticClass(), InitRotationModule::GetStaticClass(),
		GravityModule::GetStaticClass(), DragModule::GetStaticClass(), AccelerationModule::GetStaticClass(),
		ColorOverLifeModule::GetStaticClass(), SizeOverLifeModule::GetStaticClass(), SizeBySpeedModule::GetStaticClass(),
		RotationRateModule::GetStaticClass(), SubUVAnimationModule::GetStaticClass(), KillWhenSlowModule::GetStaticClass(),
		SpriteRendererModule::GetStaticClass(), RibbonRendererModule::GetStaticClass(),
		ParticleEmitterOutputNode::GetStaticClass(), ParticleAttributeNode::GetStaticClass(),
		ParticleParameterNode::GetStaticClass(),
	};
	for (TypeInfo* type : moduleTypes) mNodeGraphEditor.Registry().Register(type->TypeName, &sParticleModuleNodeView);
	TypeInfo* curveTypes[] = { ColorOverLifeModule::GetStaticClass(), SizeOverLifeModule::GetStaticClass(), SizeBySpeedModule::GetStaticClass() };
	for (TypeInfo* type : curveTypes) mNodeGraphEditor.Registry().Register(type->TypeName, &sParticleCurveParamNodeView);

	mNodeGraphEditor.LoadLayout(GetLayoutPath());

	// Select the first emitter so the canvas is not empty on open.
	TUsePointer<ParticleSystem> system = GetSystem();
	if (system && !system->Emitters.IsEmpty() && system->Emitters[0]) mSelectedEmitter = system->Emitters[0]->Uuid;

	AddPanel(ParticleGraphPanel::GetStaticClass(), false);
	AddPanel(ParticleEmittersPanel::GetStaticClass(), false);
	AddPanel(ParticleParametersPanel::GetStaticClass(), false);
	AddPanel(ParticleDetailsPanel::GetStaticClass(), false);
	AddPanel(ParticlePreviewPanel::GetStaticClass(), false);
}

void Plu::ParticleSystemViewport::OnPanelRegister()
{
	ParticleGraphPanel* graphPanel = GetPanelSlow<ParticleGraphPanel>();
	ParticleEmittersPanel* emittersPanel = GetPanelSlow<ParticleEmittersPanel>();
	ParticleParametersPanel* parametersPanel = GetPanelSlow<ParticleParametersPanel>();
	ParticleDetailsPanel* detailsPanel = GetPanelSlow<ParticleDetailsPanel>();
	ParticlePreviewPanel* previewPanel = GetPanelSlow<ParticlePreviewPanel>();
	if (graphPanel && emittersPanel && parametersPanel && detailsPanel && previewPanel) {
		ImGuiID dockspaceID = GetWindowDockID();

		ImGui::DockBuilderRemoveNode(dockspaceID);
		ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetWindowSize());

		// Left column: Emitters over Parameters. Centre: graph over preview. Right: Details.
		ImGuiID left, rest;
		ImGui::DockBuilderSplitNode(dockspaceID, ImGuiDir_Left, 0.18f, &left, &rest);
		ImGuiID right, centre;
		ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.25f, &right, &centre);
		ImGuiID leftTop, leftBottom;
		ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.5f, &leftTop, &leftBottom);
		ImGuiID centreTop, centreBottom;
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Up, 0.7f, &centreTop, &centreBottom);

		ImGui::DockBuilderDockWindow(emittersPanel->GetPanelTitle().CStr(), leftTop);
		ImGui::DockBuilderDockWindow(parametersPanel->GetPanelTitle().CStr(), leftBottom);
		ImGui::DockBuilderDockWindow(graphPanel->GetPanelTitle().CStr(), centreTop);
		ImGui::DockBuilderDockWindow(previewPanel->GetPanelTitle().CStr(), centreBottom);
		ImGui::DockBuilderDockWindow(detailsPanel->GetPanelTitle().CStr(), right);
		ImGui::DockBuilderFinish(dockspaceID);
	}
}

void Plu::ParticleSystemViewport::UpdateInspectorSelection()
{
	// A fresh node click hands the inspector back from a selected parameter to the node.
	const bool nodeSelected = mNodeGraphEditor.HasSelection();
	const PluUUID current = mNodeGraphEditor.SelectedNode();
	if (nodeSelected && (!mLastNodeSelectionValid || current != mLastSelectedNode)) {
		mSelectedParameterName = String();
	}
	mLastNodeSelectionValid = nodeSelected;
	mLastSelectedNode = current;

	// The inspected instance leaves the live registry the moment PIE ends (or its component is
	// destroyed): fall back to the defaults instead of pointing at a dead instance.
	if (mInspectedInstance) {
		TUsePointer<ParticleSystem> system = GetSystem();
		bool stillLive = false;
		if (system) {
			for (ParticleSystemInstance* instance : ParticleSystemInstance::GetLiveInstances(system->Uuid.getUUID()))
				if (instance == mInspectedInstance) { stillLive = true; break; }
		}
		if (!stillLive) mInspectedInstance = nullptr;
	}
}

void Plu::ParticleSystemViewport::OnUpdate(float deltaTime)
{
	if (BeginWindow()) {
		UpdateInspectorSelection();
		UpdatePanels(deltaTime);
	}
	EndWindow();
}

ImGuiNodeEditor::EditorContext* Plu::ParticleSystemViewport::GetEmitterContext(const PluUUID& emitterUuid)
{
	if (ImGuiNodeEditor::EditorContext** found = mEmitterContexts.Find(emitterUuid.getUUID())) return *found;

	ImGuiNodeEditor::Config config;
	config.SettingsFile = nullptr; // positions are owned by NodeGraphEditor's sidecar, not ed
	ImGuiNodeEditor::EditorContext* context = ImGuiNodeEditor::CreateEditor(&config);
	mEmitterContexts.Insert(emitterUuid.getUUID(), context);
	return context;
}

void Plu::ParticleSystemViewport::DestroyEmitterContext(const PluUUID& emitterUuid)
{
	if (ImGuiNodeEditor::EditorContext** found = mEmitterContexts.Find(emitterUuid.getUUID())) {
		ImGuiNodeEditor::DestroyEditor(*found);
		mEmitterContexts.Remove(emitterUuid.getUUID());
	}
}

Plu::TUsePointer<Plu::ParticleSystem> Plu::ParticleSystemViewport::GetSystem()
{
	return gApplicationInfo->AppAssetManager->GetAssetData(GetAssetDescriptor());
}

Plu::ParticleEmitter* Plu::ParticleSystemViewport::GetSelectedEmitter()
{
	TUsePointer<ParticleSystem> system = GetSystem();
	return system ? system->FindEmitter(mSelectedEmitter) : nullptr;
}

void Plu::ParticleSystemViewport::SelectEmitter(const PluUUID& uuid)
{
	mSelectedEmitter = uuid;
	mSelectedParameterName = String();
}

Plu::TUsePointer<Plu::IParticleParameter> Plu::ParticleSystemViewport::GetSelectedParameter()
{
	if (mSelectedParameterName.IsEmpty()) return nullptr;
	if (mInspectedInstance) return mInspectedInstance->GetParameters().Find(mSelectedParameterName);
	TUsePointer<ParticleSystem> system = GetSystem();
	return system ? system->FindParameter(mSelectedParameterName) : nullptr;
}

void Plu::ParticleSystemViewport::SelectParameter(const TUsePointer<IParticleParameter>& parameter)
{
	mSelectedParameterName = parameter ? parameter->Name : String();
}

void Plu::ParticleSystemViewport::MarkGraphChanged()
{
	if (TUsePointer<ParticleSystem> system = GetSystem()) ++system->CompileRevision;
	ViewportChangedAsset();
}
