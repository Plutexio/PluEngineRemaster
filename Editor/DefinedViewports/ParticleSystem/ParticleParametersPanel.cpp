//
// Created by Plutex on 9/26/26.
//

#include "ParticleParametersPanel.h"

#include "ParticleSystemViewport.h"
#include "PluEngine/Effects/Particles/ParticleSystem.h"
#include "PluEngine/Effects/Particles/ParticleSystemInstance.h"
#include "EditorAppContext.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"

extern Plu::EditorAppContext* gEditorAppContext;

namespace
{
	// Prefixed (the editor is a UNITY_BUILD, so file-local names share one translation unit).
	Plu::String ParticleMakeUniqueParameterName(Plu::ParticleSystem& system, const Plu::String& base)
	{
		Plu::String candidate = base;
		for (int suffix = 1; system.FindParameter(candidate).IsValid(); ++suffix)
			candidate = base + Plu::String::FromInt(suffix);
		return candidate;
	}

	ImVec4 ParticleParameterTypeColor(const Plu::String& typeName)
	{
		const auto* info = Plu::ParticleParameterFactory::GetParameterTypeInfo(typeName);
		if (!info) return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
		return ImVec4(info->Color.x / 255.0f, info->Color.y / 255.0f, info->Color.z / 255.0f, 1.0f);
	}
}

Plu::String Plu::ParticleParametersPanel::GetPanelName() { return "Parameters"; }
void Plu::ParticleParametersPanel::OnClosed() {}
void Plu::ParticleParametersPanel::OnOpened() {}

void Plu::ParticleParametersPanel::OnUpdate(float deltaTime)
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

		// Layout edits (add / remove / rename / retype) move operand offsets baked into compiled ops, so
		// they bump both revisions. A VALUE edit (Details panel) bumps neither.
		auto layoutChanged = [&] {
			++system->ParametersRevision;
			viewport->MarkGraphChanged(); // bumps CompileRevision + dirties
		};

		if (ImGui::Button("Add Parameter")) ImGui::OpenPopup("AddParameterPopup");
		if (ImGui::BeginPopup("AddParameterPopup")) {
			for (const auto& entry : ParticleParameterFactory::GetFactoryMap()) {
				if (!ImGui::MenuItem(entry.first.CStr())) continue;
				TOwningPointer<IParticleParameter> parameter = ParticleParameterFactory::CreateParameter(entry.first);
				if (!parameter) continue;
				parameter->Name = ParticleMakeUniqueParameterName(*system, String("New") + entry.first);
				system->Parameters.PushBack(parameter);
				viewport->SelectParameter(parameter);
				layoutChanged();
			}
			ImGui::EndPopup();
		}
		ImGui::Separator();

		// In PIE, with a live instance of this system: pick whether the list and the Details panel edit
		// the asset's defaults or one component's live values.
		if (gEditorAppContext->EditorScenesManager->IsInPIE()) {
			DynamicArray<ParticleSystemInstance*>& liveInstances = ParticleSystemInstance::GetLiveInstances(system->Uuid.getUUID());
			if (!liveInstances.IsEmpty()) {
				ParticleSystemInstance* inspected = viewport->GetInspectedInstance();
				const String previewLabel = inspected ? inspected->DebugName : String("Defaults");
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::BeginCombo("##InstancePicker", previewLabel.CStr())) {
					if (ImGui::Selectable("Defaults", inspected == nullptr)) viewport->SetInspectedInstance(nullptr);
					for (ParticleSystemInstance* instance : liveInstances) {
						if (!instance) continue;
						ImGui::PushID(instance);
						if (ImGui::Selectable(instance->DebugName.CStr(), instance == inspected)) viewport->SetInspectedInstance(instance);
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
				ImGui::Separator();
			}
		}

		Int32 toRemove = -1;
		const String selectedName = viewport->GetSelectedParameterName();
		for (Int32 index = 0; index < static_cast<Int32>(system->Parameters.Size()); ++index) {
			TOwningPointer<IParticleParameter>& parameter = system->Parameters[index];
			if (!parameter) continue;
			ImGui::PushID(parameter.GetRaw());

			if (mRenamingParameter == parameter->Name) {
				ImGui::SetNextItemWidth(-FLT_MIN);
				const bool submitted = ImGui::InputText("##rename", mRenameBuffer, sizeof(mRenameBuffer),
					ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
				if (submitted || ImGui::IsItemDeactivated()) {
					const String newName(mRenameBuffer);
					// Names are identity (parameter nodes reference them): unique, non-empty, changed.
					if (!newName.IsEmpty() && newName != parameter->Name && !system->FindParameter(newName).IsValid()) {
						parameter->Name = newName;
						system->SyncParameterNodeNames();
						viewport->SetSelectedParameterName(newName);
						layoutChanged();
					}
					mRenamingParameter = String();
				}
				ImGui::PopID();
				continue;
			}

			const float dotRadius = ImGui::GetFontSize() * 0.28f;
			const ImVec2 dotOrigin = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddCircleFilled(
				ImVec2(dotOrigin.x + dotRadius, dotOrigin.y + ImGui::GetTextLineHeight() * 0.5f),
				dotRadius, ImGui::GetColorU32(ParticleParameterTypeColor(parameter->TypeName)));
			ImGui::Dummy(ImVec2(dotRadius * 2.0f, ImGui::GetTextLineHeight()));
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

			if (ImGui::Selectable(parameter->Name.CStr(), selectedName == parameter->Name, ImGuiSelectableFlags_AllowDoubleClick)) {
				viewport->SelectParameter(parameter);
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					mRenamingParameter = parameter->Name;
					snprintf(mRenameBuffer, sizeof(mRenameBuffer), "%s", parameter->Name.CStr());
					ImGui::SetKeyboardFocusHere();
				}
			}

			// Payload is the name (a stable key; a raw pointer could dangle).
			if (ImGui::BeginDragDropSource()) {
				ImGui::SetDragDropPayload("PARTICLE_PARAMETER", parameter->Name.CStr(), parameter->Name.Length() + 1);
				ImGui::TextUnformatted(parameter->Name.CStr());
				ImGui::EndDragDropSource();
			}

			if (ImGui::BeginPopupContextItem()) {
				viewport->SelectParameter(parameter);
				if (ImGui::MenuItem("Rename")) {
					mRenamingParameter = parameter->Name;
					snprintf(mRenameBuffer, sizeof(mRenameBuffer), "%s", parameter->Name.CStr());
				}
				if (ImGui::MenuItem("Delete")) toRemove = index;
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		if (toRemove >= 0) {
			if (selectedName == system->Parameters[toRemove]->Name) viewport->SelectParameter(nullptr);
			system->Parameters.RemoveAt(toRemove);
			// Parameter nodes that pointed at it now resolve to nothing; drop the links their retyped
			// pins no longer satisfy.
			system->ResolveParameterReferences();
			for (auto& emitter : system->Emitters) if (emitter) emitter->PruneInvalidLinks();
			layoutChanged();
		}
	}
	EndPanel();
}
