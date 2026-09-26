//
// Created by Plutex on 9/26/26.
//

#include "ParticleGraphPanel.h"

#include "ParticleSystemViewport.h"
#include "PluEngine/Effects/Particles/ParticleSystem.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleParameterNode.h"
#include "PluEngine/AssetTypes/NodeGraph/NodePin.h"

Plu::String Plu::ParticleGraphPanel::GetPanelName() { return "Graph"; }
void Plu::ParticleGraphPanel::OnClosed() {}
void Plu::ParticleGraphPanel::OnOpened() {}

void Plu::ParticleGraphPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<ParticleSystemViewport> viewport = DynamicCast<ParticleSystemViewport>(GetParentViewport());
		TUsePointer<ParticleSystem> system = viewport ? viewport->GetSystem() : nullptr;
		ParticleEmitter* emitter = viewport ? viewport->GetSelectedEmitter() : nullptr;

		if (!system || !viewport) {
			ImGui::TextDisabled("No ParticleSystem asset loaded");
		} else if (!emitter) {
			ImGui::TextDisabled("Select or add an emitter");
		} else {
			NodeGraphEditor& editor = viewport->GetNodeGraphEditor();
			ParticleSystem* systemRaw = system.GetRaw();

			// Parameter nodes carry a parameter reference: keep them out of the reflection palette and
			// offer them from the "Parameters" section / by dragging a row from the Parameters panel.
			editor.SetPaletteTypeFilter([](TypeInfo* type) {
				return type->IsDerivedOfOrSame(ParticleParameterNode::GetStaticClass());
			});

			editor.SetPinColorProvider([](const NodePin& pin, ImVec4& out) -> bool {
				if (pin.Category != EPinCategory::Data) return false;
				for (const auto& entry : ParticleParameterFactory::GetFactoryMap()) {
					if (entry.second.PinTypeName != pin.TypeId) continue;
					out = ImVec4(entry.second.Color.x / 255.0f, entry.second.Color.y / 255.0f, entry.second.Color.z / 255.0f, 1.0f);
					return true;
				}
				return false;
			});

			editor.SetExtraAddMenu([this, viewport, systemRaw, emitter, &editor](const ImVec2& canvasPos) {
				ImGui::Separator();
				ImGui::TextDisabled("Parameters");
				bool any = false;
				for (TOwningPointer<IParticleParameter>& parameter : systemRaw->Parameters) {
					if (!parameter) continue;
					any = true;
					if (ImGui::MenuItem(parameter->Name.CStr())) {
						if (ParticleModuleNode* node = systemRaw->AddParameterNode(*emitter, parameter)) {
							editor.SetSpawnedNodePosition(node->Uuid, canvasPos);
							viewport->MarkGraphChanged();
						}
					}
				}
				if (!any) ImGui::TextDisabled("  (none)");
			});

			editor.SetCanvasDropHandler([viewport, systemRaw, emitter, &editor](const ImVec2& canvasPos) {
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PARTICLE_PARAMETER");
				if (!payload) return;
				TUsePointer<IParticleParameter> parameter =
					systemRaw->FindParameter(String(static_cast<const char*>(payload->Data)));
				if (parameter) {
					if (ParticleModuleNode* node = systemRaw->AddParameterNode(*emitter, parameter)) {
						editor.SetSpawnedNodePosition(node->Uuid, canvasPos);
						viewport->MarkGraphChanged();
					}
				}
			});

			ImGuiNodeEditor::SetCurrentEditor(viewport->GetEmitterContext(emitter->Uuid));
			ImGuiNodeEditor::Begin("Particle Emitter");
			editor.Draw(emitter, [viewport] { viewport->MarkGraphChanged(); });
			ImGuiNodeEditor::End();
			ImGuiNodeEditor::SetCurrentEditor(nullptr);
		}
	}
	EndPanel();
}
