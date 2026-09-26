//
// Created by Plutex on 9/26/26.
//

#include "ParticleDetailsPanel.h"

#include "ParticleSystemViewport.h"
#include "PluEngine/Effects/Particles/ParticleSystem.h"
#include "PluEngine/Effects/Particles/ParticleSystemInstance.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleParameterNode.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"

namespace
{
	// Parameter inspector: name (read-only here — renaming goes through the Parameters panel so that
	// uniqueness and node references stay consistent), coloured type, value control. Returns true when
	// the VALUE changed. A value edit dirties the asset but never bumps a revision.
	bool ParticleDrawParameterDetails(const Plu::TUsePointer<Plu::IParticleParameter>& parameter)
	{
		ImGui::TextUnformatted("Parameter");
		ImGui::Separator();
		ImGui::TextUnformatted("Name:");
		ImGui::SameLine();
		ImGui::TextDisabled("%s", parameter->Name.CStr());
		ImGui::TextUnformatted("Type:");
		ImGui::SameLine();
		ImVec4 color(0.5f, 0.5f, 0.5f, 1.0f);
		if (const auto* info = Plu::ParticleParameterFactory::GetParameterTypeInfo(parameter->TypeName))
			color = ImVec4(info->Color.x / 255.0f, info->Color.y / 255.0f, info->Color.z / 255.0f, 1.0f);
		ImGui::TextColored(color, "%s", parameter->TypeName.CStr());
		ImGui::TextUnformatted("Value");
		ImGui::SetNextItemWidth(-FLT_MIN);
		return parameter->DrawEditorControl("##ParameterValue");
	}
}

Plu::String Plu::ParticleDetailsPanel::GetPanelName() { return "Details"; }
void Plu::ParticleDetailsPanel::OnClosed() {}
void Plu::ParticleDetailsPanel::OnOpened() {}

void Plu::ParticleDetailsPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<ParticleSystemViewport> viewport = DynamicCast<ParticleSystemViewport>(GetParentViewport());
		TUsePointer<ParticleSystem> system = viewport ? viewport->GetSystem() : nullptr;
		if (!system) {
			ImGui::TextDisabled("No ParticleSystem asset loaded");
			EndPanel();
			return;
		}

		ParticleEmitter* emitter = viewport->GetSelectedEmitter();
		NodeGraphEditor& editor = viewport->GetNodeGraphEditor();
		GraphNode* node = (emitter && editor.HasSelection()) ? emitter->FindNode(editor.SelectedNode()) : nullptr;

		// Priority: a parameter picked in the Parameters panel, a selected parameter node (its
		// parameter), a selected node, the emitter, the system.
		TUsePointer<IParticleParameter> parameter = viewport->GetSelectedParameter();
		if (!parameter.IsValid()) {
			if (auto* parameterNode = dynamic_cast<ParticleParameterNode*>(node)) parameter = parameterNode->Parameter;
		}

		if (parameter.IsValid()) {
			if (ParticleDrawParameterDetails(parameter)) {
				// A live instance's value is per-component runtime state, not asset data: never dirty the
				// asset for it (Editor/CLAUDE.md, "Czego NIE brudzić"), just bump the store's revision.
				if (viewport->GetInspectedInstance() && viewport->GetSelectedParameter().IsValid()) {
					viewport->GetInspectedInstance()->GetParameters().MarkValueChanged();
				} else {
					viewport->ViewportChangedAsset();
				}
			}
		} else if (node) {
			ImGui::TextUnformatted(node->GetDisplayName().CStr());
			ImGui::Separator();
			if (TypeSerializer<TypeInfo*>::EditorControl(node->GetClass(), node)) {
				// A property may decide pin topology; rebuild and drop links that stopped type-checking.
				emitter->RebuildAllPins();
				emitter->PruneInvalidLinks();
				viewport->MarkGraphChanged();
			}
		} else if (emitter) {
			ImGui::Text("Emitter: %s", emitter->EmitterName.CStr());
			ImGui::Separator();
			if (TypeSerializer<TypeInfo*>::EditorControl(ParticleEmitter::GetStaticClass(), emitter)) {
				viewport->MarkGraphChanged();
			}
		} else {
			ImGui::TextUnformatted("Particle System");
			ImGui::Separator();
			if (TypeSerializer<TypeInfo*>::EditorControl(ParticleSystem::GetStaticClass(), system.GetRaw())) {
				viewport->MarkGraphChanged();
			}
		}
	}
	EndPanel();
}
