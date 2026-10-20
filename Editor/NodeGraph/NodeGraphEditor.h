//
// Created by Plutex on 7/21/26.
//

#ifndef PLUENGINE_NODEGRAPHEDITOR_H
#define PLUENGINE_NODEGRAPHEDITOR_H

#include "PluEngine/Core.h"
#include "PluEngine/PluUUID.h"
#include "PluEngine/AssetTypes/NodeGraph/NodePin.h"
#include "PluEngine/AssetTypes/NodeGraph/NodeLink.h"
#include "NodeGraph/NodeViewRegistry.h"
#include "PluSTL/HashMap/HashMap.h"
#include "PluSTL/String/String.h"
#include <imgui-node-editor/imgui_node_editor.h>
#include <functional>

namespace Plu
{
	struct NodeGraph;
	struct GraphNode;
	struct TypeInfo;

	// Reusable, domain-agnostic driver for an imgui-node-editor canvas over any NodeGraph. Handles
	// drawing (via the NodeView registry), link create/delete with pin-type validation, an "Add Node"
	// palette built from reflection, node deletion, selection tracking, and editor-owned node layout
	// (positions live here + a sidecar file, never in the runtime asset).
	//
	// Must be driven between ed::Begin() and ed::End() on the current editor context (the owning
	// viewport sets the context and brackets the frame).
	class NodeGraphEditor
	{
	public:
		NodeGraphEditor() = default;

		// Draw + interact for one frame. onModified is invoked after any mutation so the caller can
		// dirty-mark the asset. Call between ed::Begin()/ed::End().
		void Draw(NodeGraph* graph, const std::function<void()>& onModified);

		NodeViewRegistry& Registry() { return mRegistry; }

		// Primary selected node, for the details panel.
		[[nodiscard]] bool HasSelection() const { return mHasSelection; }
		[[nodiscard]] const PluUUID& SelectedNode() const { return mSelectedNode; }

		// Session-stable ephemeral ids for imgui-node-editor. Used by node views while drawing.
		ax::NodeEditor::NodeId NodeId(const GraphNode* node);
		ax::NodeEditor::PinId  PinId(const PluUUID& nodeUuid, const String& pinName, EPinDirection direction);

		// Views (e.g. an inline param slider) call this after mutating a node.
		void MarkModified();

		// Editor-owned node layout persistence. Positions never enter the runtime .pluasset.
		void LoadLayout(const StringW& path);
		void SaveLayout(const StringW& path) const;

		// ---- domain customization hooks (all optional) ----
		// The reusable driver stays domain-agnostic; a concrete graph editor injects its specifics
		// through these. All are invoked only during Draw().

		// Return true to hide a node type from the generic "Add Node" reflection palette — for node
		// types that need construction arguments and are offered another way (e.g. variable nodes).
		void SetPaletteTypeFilter(std::function<bool(TypeInfo*)> filter) { mPaletteTypeFilter = std::move(filter); }
		// Draw extra entries at the bottom of the "Add Node" popup (e.g. a Variables submenu). Given
		// the canvas position where a spawned node should be placed (see SetSpawnedNodePosition).
		void SetExtraAddMenu(std::function<void(const ImVec2&)> menu) { mExtraAddMenu = std::move(menu); }
		// Handle an ImGui drag-drop payload dropped on the canvas. Given the drop position in canvas
		// space; call ImGui::AcceptDragDropPayload inside to consume it.
		void SetCanvasDropHandler(std::function<void(const ImVec2&)> handler) { mCanvasDropHandler = std::move(handler); }
		// Override a pin's dot colour by domain type. Write the colour and return true to use it;
		// return false to keep the default (flow vs data). Lets data pins be tinted per value type.
		void SetPinColorProvider(std::function<bool(const NodePin&, ImVec4&)> provider) { mPinColorProvider = std::move(provider); }
		// Colour for a pin's dot: the domain provider if it claims the pin, else the built-in default.
		[[nodiscard]] ImVec4 GetPinColor(const NodePin& pin) const;
		// Record where a node added outside the palette (drop / extra menu) should be laid out. Same
		// mechanism the palette uses for its spawned nodes.
		void SetSpawnedNodePosition(const PluUUID& nodeUuid, const ImVec2& canvasPos);

	private:
		void DrawNodes(NodeGraph* graph);
		void DrawLinks(NodeGraph* graph);
		void HandleCreate(NodeGraph* graph);
		void HandleDelete(NodeGraph* graph);
		void HandleCanvasDrop(NodeGraph* graph);
		void HandleContextMenus(NodeGraph* graph);
		// Contents of the "Add Node" popup: search box + one submenu per node category, built from
		// reflection (every non-abstract type the graph accepts).
		void DrawAddNodeMenu(NodeGraph* graph);
		void SyncSelection();
		void ApplyStoredPositions(NodeGraph* graph);
		void CapturePositions(NodeGraph* graph);

		uintptr_t AllocNodeId(const PluUUID& uuid);
		uintptr_t AllocPinId(const PluUUID& nodeUuid, const String& pinName, EPinDirection direction);
		uintptr_t AllocLinkId(const NodeLink& link);
		static String PinKey(const PluUUID& nodeUuid, const String& pinName, EPinDirection direction);
		static String LinkKey(const NodeLink& link);

		struct PinRef
		{
			PluUUID Node;
			String  Pin;
			EPinDirection Direction = EPinDirection::Input;
		};

		NodeViewRegistry mRegistry;

		uintptr_t mNextId = 1;
		HashMap<UInt64, uintptr_t>   mNodeIdByUuid;
		HashMap<uintptr_t, UInt64>   mNodeUuidById;
		HashMap<String, uintptr_t>   mPinIdByKey;
		HashMap<uintptr_t, PinRef>   mPinById;
		HashMap<String, uintptr_t>   mLinkIdByKey;
		HashMap<uintptr_t, NodeLink> mLinkById;

		HashMap<UInt64, ImVec2> mNodePositions;  // node uuid -> canvas position (layout)
		HashMap<UInt64, bool>   mPositionApplied; // nodes already pushed into ed this session

		ImVec2 mSpawnCanvasPos = ImVec2(0.0f, 0.0f); // where the next palette-added node lands
		ax::NodeEditor::NodeId mContextNode = 0;      // node under the context menu

		bool mHasSelection = false;
		PluUUID mSelectedNode;

		std::function<bool(TypeInfo*)>     mPaletteTypeFilter; // hide types from the palette
		std::function<void(const ImVec2&)> mExtraAddMenu;      // extra "Add Node" popup content
		std::function<void(const ImVec2&)> mCanvasDropHandler; // canvas drag-drop payload handler
		std::function<bool(const NodePin&, ImVec4&)> mPinColorProvider; // per-type pin dot colour

		const std::function<void()>* mOnModified = nullptr; // valid only during Draw()
	};
}

#endif //PLUENGINE_NODEGRAPHEDITOR_H
