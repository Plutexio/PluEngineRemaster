//
// Created by Plutex on 7/21/26.
//

#include "PluEngine/AssetTypes/NodeGraph/GraphNode.h"
#include "PluEngine/AssetTypes/NodeGraph/NodeGraph.h"
#include "PluEngine/Core/Reflection/ReflectionBase.h"

namespace Plu
{
	// Reflected property types that map cleanly onto a Data pin. TypeId of the pin is exactly the
	// property's reflected type name, so connection validation is a plain string compare.
	static bool IsPinnableDataType(const String& typeName)
	{
		return typeName == "float" || typeName == "double"
			|| typeName == "bool"
			|| typeName == "int"    || typeName == "Int32"  || typeName == "UInt32"
			|| typeName == "Int64"  || typeName == "UInt64"
			|| typeName == "Vec2"   || typeName == "Vec3"   || typeName == "Vec4";
	}

	String GraphNode::GetDisplayName()
	{
		return GetClass()->TypeName;
	}

	void GraphNode::AddPin(const String& name, EPinDirection direction, EPinCategory category, const String& typeId)
	{
		NodePin pin(name, direction, category, typeId);
		if (direction == EPinDirection::Input) {
			InputPins.PushBack(pin);
		} else {
			OutputPins.PushBack(pin);
		}
	}

	void GraphNode::BuildDataPinsFromReflection()
	{
		// Walk the whole base chain so inherited PLU_PROPERTY fields also become pins.
		for (TypeInfo* type = GetClass(); type != nullptr; type = type->BaseType) {
			for (IPropertyInfo* prop : type->Properties) {
				if (!prop) continue;
				if (prop->PropertyName == "Uuid") continue; // identity, not a data input
				if (!IsPinnableDataType(prop->PropertyTypeName)) continue;
				AddPin(prop->PropertyName, EPinDirection::Input, EPinCategory::Data, prop->PropertyTypeName);
			}
		}
	}

	NodePin* GraphNode::FindPin(const String& name, EPinDirection direction)
	{
		DynamicArray<NodePin>& pins = (direction == EPinDirection::Input) ? InputPins : OutputPins;
		for (NodePin& pin : pins) {
			if (pin.Name == name) return &pin;
		}
		return nullptr;
	}

	GraphNode* GraphNode::BeginReadDataPin(GraphEvalContext& context, const String& pinName,
	                                       String& outSourcePin, String& outTypeId) const
	{
		if (!context.Graph) return nullptr;

		const NodePin* pin = FindPin(pinName, EPinDirection::Input);
		if (!pin) return nullptr;

		const NodeLink* link = context.Graph->FindInputLink(Uuid, pinName);
		if (!link) return nullptr;

		GraphNode* source = context.Graph->FindNode(link->FromNode);
		if (!source) return nullptr;

		// Data evaluation is pull-based recursion with no topological sort: a cycle would recurse
		// until the stack dies. Refuse to descend into a node that is already being evaluated and let
		// the caller fall back to its authored default — one warning, not one per frame per node.
		if (context.DataEvalStack.Contains(source->Uuid)) {
			PLU_CORE_WARN("Data cycle in graph at node {} pin '{}' — using the pin's default value",
			              source->GetDisplayName().CStr(), pinName.CStr());
			return nullptr;
		}

		outSourcePin = link->FromPin;
		outTypeId    = pin->TypeId;
		context.DataEvalStack.PushBack(source->Uuid);
		return source;
	}

	void GraphNode::EndReadDataPin(GraphEvalContext& context)
	{
		if (context.DataEvalStack.Size() > 0) context.DataEvalStack.PopBack();
	}

	const NodePin* GraphNode::FindPin(const String& name, EPinDirection direction) const
	{
		const DynamicArray<NodePin>& pins = (direction == EPinDirection::Input) ? InputPins : OutputPins;
		for (const NodePin& pin : pins) {
			if (pin.Name == name) return &pin;
		}
		return nullptr;
	}
}
