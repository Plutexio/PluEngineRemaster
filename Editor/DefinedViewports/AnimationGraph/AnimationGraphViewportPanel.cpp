//
// Created by Plutex on 7/20/26.
//

#include "AnimationGraphViewportPanel.h"

#include "AnimationGraphViewport.h"
#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/AnimationGraph/AnimationGraph.h"
#include "PluEngine/AssetTypes/AnimationGraph/Nodes/AnimVariableNode.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetTypes/NodeGraph/NodePin.h"
#include "PluEngine/Core/Reflection/ReflectionBase.h"

extern Plu::ApplicationInfo* gApplicationInfo;

namespace
{
	// Type color (0-255 → ImGui 0-1) used to tint the "Variables" menu entries, matching the list.
	ImVec4 AnimGraphMenuVariableTypeColor(const Plu::String& typeName)
	{
		const Plu::AnimationGraphVariableFactory::VariableTypeInfo* info =
			Plu::AnimationGraphVariableFactory::GetVariableTypeInfo(typeName);
		if (!info) {
			return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
		}
		return ImVec4(info->Color.x / 255.0f, info->Color.y / 255.0f, info->Color.z / 255.0f, 1.0f);
	}
}

Plu::String Plu::AnimationGraphViewportPanel::GetPanelName()
{
	return "Viewport";
}

void Plu::AnimationGraphViewportPanel::OnClosed()
{
}

void Plu::AnimationGraphViewportPanel::OnOpened()
{
}

void Plu::AnimationGraphViewportPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<AnimationGraph> animationGraph =
			gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
		TUsePointer<AnimationGraphViewport> viewport = DynamicCast<AnimationGraphViewport>(GetParentViewport());

		if (animationGraph && viewport) {
			NodeGraphEditor& editor = viewport->GetNodeGraphEditor();
			AnimationGraph* graph = animationGraph.GetRaw();

			// Inject the animation-graph specifics into the reusable driver. Re-set each frame so the
			// captured graph pointer never goes stale (the hooks run only inside Draw, this frame).

			// Variable nodes carry a variable reference, so keep them out of the generic reflection
			// palette (it would build one with no variable). They come from the "Variables" section
			// and from dragging a row out of the Variables panel instead.
			editor.SetPaletteTypeFilter([](TypeInfo* type) {
				return type->IsDerivedOfOrSame(AnimVariableNode::GetStaticClass());
			});

			// Tint each data pin in its value type's colour (float pins the Float colour, etc.), so a
			// node's inputs read as the same colours as the variables that can drive them. Flow pins
			// and unregistered data types fall back to the driver's default.
			editor.SetPinColorProvider([](const NodePin& pin, ImVec4& out) -> bool {
				if (pin.Category != EPinCategory::Data) return false;
				const Vec3* color = AnimationGraphVariableFactory::GetColorForPinType(pin.TypeId);
				if (!color) return false;
				out = ImVec4(color->x / 255.0f, color->y / 255.0f, color->z / 255.0f, 1.0f);
				return true;
			});

			// A "Variables" section at the bottom of the Add-Node popup: one entry per variable,
			// tinted in its type colour. Selecting one spawns a bound variable node at the popup.
			editor.SetExtraAddMenu([this, graph, &editor](const ImVec2& canvasPos) {
				ImGui::Separator();
				ImGui::TextDisabled("Variables");
				bool any = false;
				for (TOwningPointer<IAnimationGraphVariable>& variable : graph->Variables) {
					if (!variable) continue;
					any = true;
					ImGui::PushStyleColor(ImGuiCol_Text, AnimGraphMenuVariableTypeColor(variable->TypeName));
					const bool clicked = ImGui::MenuItem(variable->Name.CStr());
					ImGui::PopStyleColor();
					if (clicked) {
						if (AnimVariableNode* node = graph->AddVariableNode(variable)) {
							editor.SetSpawnedNodePosition(node->Uuid, canvasPos);
							PanelChangedAsset();
						}
					}
				}
				if (!any) ImGui::TextDisabled("  (none)");
			});

			// Dropping a variable row onto the canvas spawns a bound node where it landed.
			editor.SetCanvasDropHandler([this, graph, &editor](const ImVec2& canvasPos) {
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ANIMGRAPH_VARIABLE");
				if (!payload) return;
				TUsePointer<IAnimationGraphVariable> variable =
					graph->FindVariable(String(static_cast<const char*>(payload->Data)));
				if (variable) {
					if (AnimVariableNode* node = graph->AddVariableNode(variable)) {
						editor.SetSpawnedNodePosition(node->Uuid, canvasPos);
						PanelChangedAsset();
					}
				}
			});

			// Some nodes derive their pins from another asset (AnimBlendSpaceNode: axis count and
			// names of a BlendSpace edited in its own window) — rebuild when that changed. Only a
			// pruned link is a real edit of this graph; relabelled pins are not.
			bool pinsStale = false;
			for (TOwningPointer<GraphNode>& node : graph->Nodes) {
				if (node && node->ArePinsStale()) { pinsStale = true; break; }
			}
			if (pinsStale) {
				const UInt64 linkCount = graph->Links.Size();
				graph->RebuildAllPins();
				graph->PruneInvalidLinks();
				if (graph->Links.Size() != linkCount) PanelChangedAsset();
			}

			ImGuiNodeEditor::SetCurrentEditor(viewport->GetNodeEditorContext());
			ImGuiNodeEditor::Begin("Animation Graph");

			// The reusable driver does everything: draw, link create/delete, add-node palette,
			// selection, layout. It dirties the asset via the callback on any mutation.
			editor.Draw(graph, [this] { PanelChangedAsset(); });

			ImGuiNodeEditor::End();
			ImGuiNodeEditor::SetCurrentEditor(nullptr);
		}
	}
	EndPanel();
}
