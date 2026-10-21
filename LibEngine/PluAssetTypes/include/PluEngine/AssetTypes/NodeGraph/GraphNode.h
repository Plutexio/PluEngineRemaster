//
// Created by Plutex on 7/21/26.
//

#ifndef PLUENGINE_GRAPHNODE_H
#define PLUENGINE_GRAPHNODE_H

#include "PluEngine/Core.h"
#include "PluBase/PluUUID.h"
#include "PluEngine/AssetTypes/NodeGraph/NodePin.h"
#include "PluEngine/AssetTypes/NodeGraph/GraphEvalContext.h"
#include "PluSTL/Array/Array.h"
#include "PluSTL/String/String.h"
#include "GraphNode.generated.h"

namespace Plu
{
	// Reusable base for every node in every graph domain. A plain reflected polymorphic class
	// (NOT an EngineObject) — same shape as IAssetData: we get factory-by-name (TypeInfo::Construct),
	// property enumeration for auto pin/param UI, and typeName-tagged polymorphic JSON, without the
	// per-object EngineObjectManager overhead. Domain category bases (e.g. AnimGraphNode) derive this
	// and add their flow-pin kind + evaluation contract; concrete nodes derive those.
	//
	// Editor-only concerns (canvas position, rendering) live in the editor, never here.
	//
	// A PLU_STRUCT (not PLU_CLASS): the generator gives PLU_CLASS an unconditional `GetClass()
	// override` assuming an EngineObject base, which a non-EngineObject root can't have. PLU_STRUCT
	// resolves virtual/override correctly for a reflected-struct hierarchy — same as IAssetData.
	PLU_STRUCT(Abstract)
	struct PLUASSETTYPES_API GraphNode
	{
		REFLECTION_BODY_GRAPHNODE()
	public:
		GraphNode() = default;
		virtual ~GraphNode() = default;

		// Stable identity across saves; link endpoints reference it. Random by default (PluUUID ctor).
		PLU_PROPERTY()
		PluUUID Uuid;

		// (Re)built by BuildPins(), never serialized. Concrete nodes fill these.
		DynamicArray<NodePin> InputPins;
		DynamicArray<NodePin> OutputPins;

		// Declare this node's pins. Category bases add their flow pins; concrete nodes add the rest
		// (often via BuildDataPinsFromReflection()). Called on construction and on RebuildAllPins().
		virtual void BuildPins() {}

		// Whether the pin set (or its labels) depends on data outside the node that changed since the
		// last BuildPins() — e.g. AnimBlendSpaceNode's Y pin exists only for a 2D blend space asset,
		// which can be edited in its own editor. Editors poll this and RebuildAllPins() +
		// PruneInvalidLinks() when it fires. Default: pins depend on the node's own properties only.
		[[nodiscard]] virtual bool ArePinsStale() const { return false; }

		// Title shown on the node header. Defaults to the reflected type name.
		virtual String GetDisplayName();

		// Writes this node's `pinName` data output into outValue (a pointer to storage of the type
		// named by typeId) and returns whether it did. Default: no data outputs. Overridden by every
		// node that exposes one (variable nodes, the whole math/logic/vector family). Lives here and
		// not on a domain base so value nodes are usable in every graph domain.
		virtual bool EvaluateDataOutput(GraphEvalContext& context, const String& pinName,
		                                const String& typeId, void* outValue) { return false; }

		// Adds one Data input pin per reflected PLU_PROPERTY whose type maps to a pin type
		// ("reszta pinów po typach z refleksji"). Skips the Uuid identity field.
		void BuildDataPinsFromReflection();

		NodePin* FindPin(const String& name, EPinDirection direction);
		[[nodiscard]] const NodePin* FindPin(const String& name, EPinDirection direction) const;

	protected:
		void AddPin(const String& name, EPinDirection direction, EPinCategory category, const String& typeId);

		// Follows this node's `pinName` data input across its link to the source node and reads its
		// value there (EvaluateDataOutput, matched against this pin's own declared TypeId — see
		// BuildDataPinsFromReflection). Unconnected input, missing source, a type mismatch or a data
		// cycle: returns `fallback` (the node's own authored default for that pin). This is the
		// pattern for every data-pin-driven node property: read through ReadDataPin, the node's own
		// PLU_PROPERTY as the fallback — e.g. AnimBlendNode::Alpha.
		template <typename T>
		[[nodiscard]] T ReadDataPin(GraphEvalContext& context, const String& pinName, const T& fallback) const
		{
			String sourcePin;
			String typeId;
			GraphNode* source = BeginReadDataPin(context, pinName, sourcePin, typeId);
			if (!source) return fallback;

			T value = fallback;
			const bool read = source->EvaluateDataOutput(context, sourcePin, typeId, &value);
			EndReadDataPin(context);
			return read ? value : fallback;
		}

	private:
		// Type-independent half of ReadDataPin (needs a complete NodeGraph, so it can't live in the
		// header): resolves the link on `pinName` to its source node, hands back which output pin to
		// read and under which type id, and pushes the source onto the context's cycle stack. Null
		// when the pin is unconnected, the source is gone, or the source is already being evaluated
		// (data cycle) — in which case nothing is pushed and EndReadDataPin must not be called.
		GraphNode* BeginReadDataPin(GraphEvalContext& context, const String& pinName,
		                            String& outSourcePin, String& outTypeId) const;
		static void EndReadDataPin(GraphEvalContext& context);
	};
}

#endif //PLUENGINE_GRAPHNODE_H
