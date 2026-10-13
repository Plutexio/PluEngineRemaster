//
// Created by Plutex on 7/23/26.
//

#include "AnimationGraphVariablesPanel.h"

#include "AnimationGraphViewport.h"
#include "EditorAppContext.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/AnimationGraph/AnimationGraph.h"
#include "PluEngine/AssetTypes/Animation/AnimGraphInstance.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/Core/Widgets/Combo.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"

extern Plu::ApplicationInfo* gApplicationInfo;
extern Plu::EditorAppContext* gEditorAppContext;

namespace
{
	// Prefixed (the editor is a UNITY_BUILD, so file-local names share one translation unit).

	// First of `base`, `base1`, `base2`, ... not already used by a variable in this graph — e.g.
	// base "NewInteger" → "NewInteger", then "NewInteger1". Variables are addressed by Name (a
	// variable node references one), so names must be unique.
	Plu::String AnimGraphMakeUniqueVariableName(const Plu::TUsePointer<Plu::AnimationGraph>& graph,
	                                            const Plu::String& base)
	{
		auto isTaken = [&](const Plu::String& candidate) {
			for (const auto& variable : graph->Variables)
				if (variable && variable->Name == candidate) return true;
			return false;
		};

		Plu::String candidate = base;
		for (int suffix = 1; isTaken(candidate); ++suffix)
			candidate = base + Plu::String::FromInt(suffix);
		return candidate;
	}

	// Registered display colour for a variable type (0-255 → ImGui 0-1), grey when the type is
	// unknown (a variable of a type no longer registered).
	ImVec4 AnimGraphVariableTypeColor(const Plu::String& typeName)
	{
		const Plu::AnimationGraphVariableFactory::VariableTypeInfo* info =
			Plu::AnimationGraphVariableFactory::GetVariableTypeInfo(typeName);
		if (!info) {
			return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
		}
		return ImVec4(info->Color.x / 255.0f, info->Color.y / 255.0f, info->Color.z / 255.0f, 1.0f);
	}
}

Plu::String Plu::AnimationGraphVariablesPanel::GetPanelName()
{
	return "Variables";
}

void Plu::AnimationGraphVariablesPanel::OnClosed()
{
}

void Plu::AnimationGraphVariablesPanel::OnOpened()
{
}

void Plu::AnimationGraphVariablesPanel::BeginRename(const TUsePointer<IAnimationGraphVariable>& variable)
{
	mRenamingVariable   = variable;
	mRenameFocusPending = true;
	const String& name  = variable->Name;
	const UInt64 length = name.Length() < sizeof(mRenameBuffer) - 1 ? name.Length() : sizeof(mRenameBuffer) - 1;
	memcpy(mRenameBuffer, name.CStr(), length);
	mRenameBuffer[length] = '\0';
}

void Plu::AnimationGraphVariablesPanel::CommitRename(const TUsePointer<AnimationGraph>& graph,
                                                     const TUsePointer<IAnimationGraphVariable>& variable)
{
	const String newName(mRenameBuffer);
	auto isTaken = [&](const String& candidate) {
		for (const auto& other : graph->Variables)
			if (other && other.GetRaw() != variable.GetRaw() && other->Name == candidate) return true;
		return false;
	};

	// Ignore empty / unchanged / already-taken names — the rename just reverts silently, so Esc
	// (which restores the old text into the buffer) never dirties the asset.
	if (variable && !newName.IsEmpty() && newName != variable->Name && !isTaken(newName)) {
		variable->Name = newName;
		++graph->VariablesRevision;
		PanelChangedAsset();
	}
	mRenamingVariable   = nullptr;
	mRenameFocusPending = false;
}

void Plu::AnimationGraphVariablesPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<AnimationGraph> animationGraph =
			gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
		TUsePointer<AnimationGraphViewport> viewport = DynamicCast<AnimationGraphViewport>(GetParentViewport());

		if (!animationGraph || !viewport) {
			ImGui::TextDisabled("No AnimationGraph asset loaded");
			EndPanel();
			return;
		}

		// "Add Variable" opens a palette of every registered variable type — the keys of the factory
		// map (Integer/Float/Bool/String/Vec3, see AnimationGraphVariableFactory::RegisterBuiltInTypes).
		// Adding a type there is the only change needed for it to show up here.
		if (ImGui::Button("Add Variable")) {
			ImGui::OpenPopup("AddVariablePopup");
		}
		if (ImGui::BeginPopup("AddVariablePopup")) {
			for (const auto& entry : AnimationGraphVariableFactory::GetFactoryMap()) {
				if (ImGui::MenuItem(entry.first.CStr())) {
					TOwningPointer<IAnimationGraphVariable> variable =
						AnimationGraphVariableFactory::CreateVariable(entry.first);
					if (variable) {
						// Default name from the type: "NewInteger", "NewFloat", ...
						variable->Name = AnimGraphMakeUniqueVariableName(animationGraph, String("New") + entry.first);
						animationGraph->Variables.PushBack(variable);
						viewport->SelectVariable(variable);
						++animationGraph->VariablesRevision;
						PanelChangedAsset();
					}
				}
			}
			ImGui::EndPopup();
		}
		ImGui::Separator();

		// In PIE, with at least one live instance of this graph: pick whether the list below (and the
		// Details panel) edits the asset's defaults or one actor's live values. Outside PIE, or with
		// no live instances yet, there is nothing to pick — stays on Defaults.
		if (gEditorAppContext->EditorScenesManager->IsInPIE()) {
			DynamicArray<AnimGraphInstance*>& liveInstances =
				AnimGraphInstance::GetLiveInstances(animationGraph->Uuid.getUUID());
			if (!liveInstances.IsEmpty()) {
				AnimGraphInstance* inspected = viewport->GetInspectedInstance();
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGuiWidgets::ComboValue("##InstancePicker", &inspected, liveInstances,
						[](AnimGraphInstance* instance) { return instance->DebugName.CStr(); }, "Defaults")) {
					viewport->SetInspectedInstance(inspected);
				}
				ImGui::Separator();
			}
		}

		// Deferred: removing inside the loop would invalidate the iteration.
		Int32 variableToRemove = -1;
		const String& selectedName = viewport->GetSelectedVariableName();
		for (Int32 index = 0; index < static_cast<Int32>(animationGraph->Variables.Size()); ++index) {
			const TOwningPointer<IAnimationGraphVariable>& variable = animationGraph->Variables[index];
			if (!variable) {
				continue;
			}
			// PushID keeps the row's widgets unique per variable regardless of Name.
			ImGui::PushID(variable.GetRaw());

			if (mRenamingVariable.IsValid() && mRenamingVariable.GetRaw() == variable.GetRaw()) {
				if (mRenameFocusPending) {
					ImGui::SetKeyboardFocusHere();
					mRenameFocusPending = false;
				}
				ImGui::SetNextItemWidth(-FLT_MIN);
				const bool submitted = ImGui::InputText("##rename", mRenameBuffer, sizeof(mRenameBuffer),
					ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
				// IsItemDeactivated catches both Esc and a click outside. Esc restores the old text
				// into the buffer, so CommitRename then writes nothing.
				if (submitted || ImGui::IsItemDeactivated()) {
					CommitRename(animationGraph, variable);
				}
				ImGui::PopID();
				continue;
			}

			// A filled dot in the variable type's colour, then the name. The dot is drawn manually
			// and its width reserved with a Dummy so the Selectable (full remaining width) still
			// gives the row a proper selection highlight.
			const float dotRadius = ImGui::GetFontSize() * 0.28f;
			const ImVec2 dotOrigin = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddCircleFilled(
				ImVec2(dotOrigin.x + dotRadius, dotOrigin.y + ImGui::GetTextLineHeight() * 0.5f),
				dotRadius, ImGui::GetColorU32(AnimGraphVariableTypeColor(variable->TypeName)));
			ImGui::Dummy(ImVec2(dotRadius * 2.0f, ImGui::GetTextLineHeight()));
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

			if (ImGui::Selectable(variable->Name.CStr(), selectedName == variable->Name,
				ImGuiSelectableFlags_AllowDoubleClick)) {
				viewport->SelectVariable(variable);
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					BeginRename(variable);
				}
			}

			// Drag onto the graph canvas to drop a variable node. Payload is the variable name (a
			// stable key; a raw pointer could dangle) — the canvas handler looks it up on drop.
			if (ImGui::BeginDragDropSource()) {
				ImGui::SetDragDropPayload("ANIMGRAPH_VARIABLE",
					variable->Name.CStr(), variable->Name.Length() + 1);
				ImGui::TextUnformatted(variable->Name.CStr());
				ImGui::EndDragDropSource();
			}

			if (ImGui::BeginPopupContextItem()) {
				viewport->SelectVariable(variable);
				if (ImGui::Button("Rename")) {
					BeginRename(variable);
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::Button("Delete")) {
					variableToRemove = index;
					ImGui::CloseCurrentPopup();
				}
				ImGui::Separator();
				if (ImGui::Button("Close")) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			ImGui::PopID();
		}

		if (variableToRemove >= 0) {
			// Clear the inspector if it was pointing at the variable we're about to drop.
			if (selectedName == animationGraph->Variables[variableToRemove]->Name) {
				viewport->SelectVariable(nullptr);
			}
			animationGraph->Variables.RemoveAt(variableToRemove);
			++animationGraph->VariablesRevision;
			PanelChangedAsset();
		}
	}
	EndPanel();
}
