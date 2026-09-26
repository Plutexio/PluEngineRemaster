//
// Created by Plutex on 9/26/26.
//

#include "ParticleEmittersPanel.h"

#include "ParticleSystemViewport.h"
#include "PluEngine/Effects/Particles/ParticleSystem.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleEmitterOutputNode.h"

Plu::String Plu::ParticleEmittersPanel::GetPanelName() { return "Emitters"; }
void Plu::ParticleEmittersPanel::OnClosed() {}
void Plu::ParticleEmittersPanel::OnOpened() {}

void Plu::ParticleEmittersPanel::OnUpdate(float deltaTime)
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

		if (ImGui::Button("Add Emitter")) {
			String name = "Emitter";
			for (int suffix = 1;; ++suffix) {
				bool taken = false;
				for (auto& other : system->Emitters) if (other && other->EmitterName == name) taken = true;
				if (!taken) break;
				name = String("Emitter") + String::FromInt(suffix);
			}
			if (ParticleEmitter* emitter = system->AddEmitter(name)) {
				// A fresh emitter starts with its chain terminator so it is compilable once modules are wired.
				emitter->AddNode(ParticleEmitterOutputNode::GetStaticClass());
				viewport->SelectEmitter(emitter->Uuid);
				viewport->MarkGraphChanged();
			}
		}
		ImGui::Separator();

		// Deferred structural edits: mutating the array inside the loop would invalidate iteration.
		Int32 toRemove = -1, moveFrom = -1, moveTo = -1;
		for (Int32 index = 0; index < static_cast<Int32>(system->Emitters.Size()); ++index) {
			ParticleEmitter* emitter = system->Emitters[index].GetRaw();
			if (!emitter) continue;
			ImGui::PushID(emitter);

			if (mRenamingEmitter == emitter->Uuid) {
				ImGui::SetNextItemWidth(-FLT_MIN);
				const bool submitted = ImGui::InputText("##rename", mRenameBuffer, sizeof(mRenameBuffer),
					ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
				if (submitted || ImGui::IsItemDeactivated()) {
					// Empty / unchanged names revert silently, so Esc never dirties the asset.
					if (mRenameBuffer[0] != '\0' && emitter->EmitterName != mRenameBuffer) {
						emitter->EmitterName = mRenameBuffer;
						PanelChangedAsset();
					}
					mRenamingEmitter = PluUUID(0);
				}
				ImGui::PopID();
				continue;
			}

			if (ImGui::Checkbox("##enabled", &emitter->Enabled)) viewport->MarkGraphChanged();
			ImGui::SameLine();
			if (ImGui::Selectable(emitter->EmitterName.CStr(), viewport->GetSelectedEmitterUuid() == emitter->Uuid,
				ImGuiSelectableFlags_AllowDoubleClick)) {
				viewport->SelectEmitter(emitter->Uuid);
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					mRenamingEmitter = emitter->Uuid;
					snprintf(mRenameBuffer, sizeof(mRenameBuffer), "%s", emitter->EmitterName.CStr());
					ImGui::SetKeyboardFocusHere();
				}
			}

			if (ImGui::BeginPopupContextItem()) {
				viewport->SelectEmitter(emitter->Uuid);
				if (ImGui::MenuItem("Rename")) {
					mRenamingEmitter = emitter->Uuid;
					snprintf(mRenameBuffer, sizeof(mRenameBuffer), "%s", emitter->EmitterName.CStr());
				}
				if (ImGui::MenuItem("Move Up", nullptr, false, index > 0)) { moveFrom = index; moveTo = index - 1; }
				if (ImGui::MenuItem("Move Down", nullptr, false, index + 1 < static_cast<Int32>(system->Emitters.Size()))) {
					moveFrom = index; moveTo = index + 1;
				}
				if (ImGui::MenuItem("Delete")) toRemove = index;
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		if (moveFrom >= 0) {
			TOwningPointer<ParticleEmitter> moved = system->Emitters[moveFrom];
			system->Emitters[moveFrom] = system->Emitters[moveTo];
			system->Emitters[moveTo] = moved;
			viewport->MarkGraphChanged();
		}
		if (toRemove >= 0) {
			const PluUUID removedUuid = system->Emitters[toRemove]->Uuid;
			viewport->DestroyEmitterContext(removedUuid);
			system->Emitters.RemoveAt(toRemove);
			if (viewport->GetSelectedEmitterUuid() == removedUuid) {
				viewport->SelectEmitter(system->Emitters.IsEmpty() ? PluUUID(0) : system->Emitters[0]->Uuid);
			}
			viewport->MarkGraphChanged();
		}
	}
	EndPanel();
}
