//
// Created by Plutex on 7/21/26.
//

#ifndef PLUENGINE_NODEGRAPH_H
#define PLUENGINE_NODEGRAPH_H

#include "PluEngine/Core.h"
#include "PluEngine/Core/IAssetData.h"
#include "PluEngine/AssetTypes/NodeGraph/GraphNode.h"
#include "PluEngine/AssetTypes/NodeGraph/NodeLink.h"
#include "PluSTL/Array/Array.h"
#include "PluSTL/Pointers/TOwningPointer.h"
#include "NodeGraph.generated.h"

namespace Plu
{
	struct TypeInfo;

	// Reusable node-graph asset base: owns a polymorphic node list + the links between them.
	// Concrete graph assets (e.g. AnimationGraph) derive this and override GetNodeBaseType() so the
	// editor palette lists the right node family. Serialization of Nodes/Links is done by
	// NodeGraphSerializer (the generic reflection array serializer is non-polymorphic and would drop
	// subclass identity) — hence Nodes/Links are deliberately NOT PLU_PROPERTY.
	PLU_STRUCT()
	struct PLUASSETTYPES_API NodeGraph : IAssetData
	{
		REFLECTION_BODY_NODEGRAPH()

		DynamicArray<TOwningPointer<GraphNode>> Nodes;
		DynamicArray<NodeLink> Links;

		~NodeGraph() override = default;

		// Node family this graph accepts. Drives the "Add Node" palette. Default: any GraphNode.
		virtual TypeInfo* GetNodeBaseType();

		// Whether a node type may be placed in this graph. Default: the graph's own node family, plus
		// every DataGraphNode — value nodes (math/vector/logic/convert) only ever read data pins and
		// write data outputs, so they are meaningful in every graph domain and are not worth
		// re-declaring per domain. Overriding graphs should keep calling the base for that reason.
		virtual bool AcceptsNodeType(TypeInfo* nodeType);

		// Constructs a node of nodeType (must pass AcceptsNodeType), builds its pins, appends it.
		// Returns a non-owning view (owned by Nodes). Null if nodeType is null / not constructible.
		GraphNode* AddNode(TypeInfo* nodeType);

		// Removes the node and every link touching it.
		void RemoveNode(const PluUUID& nodeId);

		GraphNode* FindNode(const PluUUID& nodeId);

		// Finds the node feeding an input pin, following the one link (if any) that targets
		// (toNode, toPin). Null when the pin is unconnected or the source node no longer exists.
		GraphNode* GetLinkSource(const PluUUID& toNode, const String& toPin);

		// The link (if any) whose target is (toNode, toPin) — the source-name-carrying counterpart
		// to GetLinkSource, needed by data-pin reads (AnimGraphNode::ReadDataPin) which must know
		// which output pin on the source node to read, not just which node.
		[[nodiscard]] const NodeLink* FindInputLink(const PluUUID& toNode, const String& toPin) const;

		// Adds a link if the two pins are compatible (NodePin::CanConnect) and not already linked.
		// Pins are addressed by (node uuid, pin name). Returns whether a link was added.
		bool Connect(const PluUUID& fromNode, const String& fromPin,
		             const PluUUID& toNode, const String& toPin);

		void Disconnect(const NodeLink& link);

		// Clears and rebuilds every node's pins (after edits that change pin topology).
		void RebuildAllPins();

		// Drops every link whose endpoints no longer exist or no longer type-check. Pin sets are
		// rebuilt from node properties (a wildcard node retypes its pins when its ValueType changes),
		// so links that were valid when authored can stop being valid — call this after RebuildAllPins.
		void PruneInvalidLinks();
	};
}

#endif //PLUENGINE_NODEGRAPH_H
