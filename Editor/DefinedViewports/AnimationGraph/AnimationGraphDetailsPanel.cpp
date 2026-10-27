//
// Created by Plutex on 7/20/26.
//

#include "AnimationGraphDetailsPanel.h"

#include "AnimationGraphViewport.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/AnimationGraph/AnimationGraph.h"
#include "PluEngine/AssetTypes/AnimationGraph/Nodes/AnimVariableNode.h"
#include "PluEngine/AssetTypes/Animation/AnimGraphInstance.h"
#include "PluEngine/AssetTypes/Animation/BoneRef.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetTypes/NodeGraph/GraphNode.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/Core/Widgets/Text.h"
#include "UI/IconsFontAwesome7.h"

extern Plu::ApplicationInfo* gApplicationInfo;

namespace
{
	// Variable inspector: name field, coloured type, value control. Shared by a variable selected in
	// the Variables panel and by a selected variable node (its underlying variable). Returns true if
	// a field changed. isLiveInstance: editing a live PIE instance's value instead of the asset's
	// default — Name/Type become read-only (identity is the asset's, not the instance's to change)
	// and the caller must NOT dirty the asset (see the "Czego NIE brudzić" rule in Editor/CLAUDE.md);
	// it bumps the store's value revision instead (AnimGraphVariableStore::MarkValueChanged).
	bool AnimGraphDrawVariableDetails(const Plu::TUsePointer<Plu::IAnimationGraphVariable>& variable, bool isLiveInstance)
	{
		using namespace Plu;
		bool changed = false;

		ImGui::TextUnformatted(isLiveInstance ? "Variable (live)" : "Variable");
		ImGui::Separator();

		ImGui::TextUnformatted("Name");
		if (isLiveInstance) {
			// Identity belongs to the asset's variable list, not this instance — read-only here.
			ImGui::TextDisabled("%s", variable->Name.CStr());
		} else {
			// Uniqueness is enforced when renaming from the Variables panel; here we accept whatever
			// is typed (a live-edited name colliding with another only matters once a node references it).
			char nameBuffer[128];
			const UInt64 nameLength = variable->Name.Length() < sizeof(nameBuffer) - 1
				? variable->Name.Length() : sizeof(nameBuffer) - 1;
			memcpy(nameBuffer, variable->Name.CStr(), nameLength);
			nameBuffer[nameLength] = '\0';

			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputText("##VariableName", nameBuffer, sizeof(nameBuffer))) {
				variable->Name = nameBuffer;
				changed = true;
			}
		}

		// "Type:" label, then the type name painted in that type's registered colour.
		ImGui::TextUnformatted("Type:");
		ImGui::SameLine();
		ImVec4 typeColor(0.5f, 0.5f, 0.5f, 1.0f);
		if (const AnimationGraphVariableFactory::VariableTypeInfo* typeInfo =
			AnimationGraphVariableFactory::GetVariableTypeInfo(variable->TypeName)) {
			typeColor = ImVec4(typeInfo->Color.x / 255.0f, typeInfo->Color.y / 255.0f,
				typeInfo->Color.z / 255.0f, 1.0f);
		}
		ImGui::TextColored(typeColor, "%s", variable->TypeName.CStr());

		ImGui::TextUnformatted("Value");
		ImGui::SetNextItemWidth(-FLT_MIN);
		// Hidden label: the name has its own field above.
		if (variable->DrawEditorControl("##VariableValue")) {
			changed = true;
		}
		return changed;
	}
}

Plu::String Plu::AnimationGraphDetailsPanel::GetPanelName()
{
	return "Details";
}

void Plu::AnimationGraphDetailsPanel::OnClosed()
{
}

void Plu::AnimationGraphDetailsPanel::OnOpened()
{
}

void Plu::AnimationGraphDetailsPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<AnimationGraph> animationGraph =
			gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
		TUsePointer<AnimationGraphViewport> viewport = DynamicCast<AnimationGraphViewport>(GetParentViewport());

		if (!animationGraph || !viewport) {
			ImGuiWidgets::EmptyState("No AnimationGraph asset loaded", ICON_FA_DIAGRAM_PROJECT);
			EndPanel();
			return;
		}

		NodeGraphEditor& editor = viewport->GetNodeGraphEditor();
		GraphNode* node = editor.HasSelection() ? animationGraph->FindNode(editor.SelectedNode()) : nullptr;

		// What the inspector shows, in priority order:
		//  1. A variable selected in the Variables panel (it "stole" the inspector from the canvas —
		//     see AnimationGraphViewport::UpdateInspectorSelection), or
		//  2. a selected variable node → its underlying variable (so you edit the variable, not the
		//     node's raw VariableName key), or
		//  3. the selected node's own properties, or
		//  4. the graph asset's properties.
		TUsePointer<IAnimationGraphVariable> variable = viewport->GetSelectedVariable();
		// A node's own Variable always points at the asset's default (ResolveVariableReferences never
		// binds it to a live instance), so that path is never "live" even while a PIE instance is
		// picked in the Variables panel.
		bool variableIsLive = variable.IsValid() && (viewport->GetInspectedInstance() != nullptr);
		if (!variable.IsValid()) {
			if (auto* variableNode = dynamic_cast<AnimVariableNode*>(node)) {
				variable = variableNode->Variable;
				variableIsLive = false;
			}
		}

		if (variable.IsValid()) {
			if (AnimGraphDrawVariableDetails(variable, variableIsLive)) {
				if (variableIsLive) {
					viewport->GetInspectedInstance()->GetVariables().MarkValueChanged();
				} else {
					PanelChangedAsset();
				}
			}
		} else if (node) {
			ImGui::TextUnformatted(node->GetDisplayName().CStr());
			ImGui::Separator();
			// Bone pickers (BoneRef properties) can't reach their owning graph from a bare void*,
			// so the skeleton they list is set here for the duration of drawing this node's properties.
			SetBonePickerSkeleton(animationGraph->TargetSkeleton.GetRaw());
			// One line renders every PLU_PROPERTY of the concrete node type; true = a field changed.
			if (TypeSerializer<TypeInfo*>::EditorControl(node->GetClass(), node)) {
				// A property can decide the node's pin topology (a value node's ValueType retypes its
				// pins), so rebuild pins and drop the links that stopped type-checking. Generic on
				// purpose: any future property that affects pins gets this for free, and it costs a
				// pass over a handful of nodes only on an actual edit.
				animationGraph->RebuildAllPins();
				animationGraph->PruneInvalidLinks();
				PanelChangedAsset();
			}
			SetBonePickerSkeleton(nullptr);
		} else {
			// Nothing selected: edit the graph asset's own properties (target skeleton, …).
			ImGui::TextUnformatted("Animation Graph");
			ImGui::Separator();
			if (TypeSerializer<TypeInfo*>::EditorControl(AnimationGraph::GetStaticClass(), animationGraph.GetRaw())) {
				PanelChangedAsset();
			}
			ImGui::Separator();
			ImGui::TextDisabled("Select a node or a variable to edit it");
		}
	}
	EndPanel();
}
