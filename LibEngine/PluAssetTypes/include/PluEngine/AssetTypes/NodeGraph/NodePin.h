//
// Created by Plutex on 7/21/26.
//

#ifndef PLUENGINE_NODEPIN_H
#define PLUENGINE_NODEPIN_H

#include "PluEngine/Core.h"
#include "PluSTL/String/String.h"

namespace Plu
{
	enum class EPinDirection
	{
		Input,
		Output
	};

	// Flow = domain "wire" (Pose for animation, Exec for logic), kind carried by TypeId.
	// Data = value connection whose TypeId is a reflected type name (float, bool, Vec3, ...).
	enum class EPinCategory
	{
		Flow,
		Data
	};

	// Runtime pin descriptor. Built each frame-independent (re)build of a node's pins via
	// GraphNode::BuildPins — NOT serialized. Links persist by (node Uuid + pin Name), so a
	// node's pin set can change freely between versions without breaking existing graphs.
	struct PLUASSETTYPES_API NodePin
	{
		String Name;                               // unique within a node+direction; link identity
		EPinDirection Direction = EPinDirection::Input;
		EPinCategory  Category  = EPinCategory::Flow;
		String TypeId;                             // Flow: flow kind ("Pose"/"Exec"); Data: type name
		// Optional text the editor shows instead of Name (e.g. a blend space axis name). Display
		// only — links keep addressing the pin by Name, so a label may change freely.
		String Label;

		[[nodiscard]] const String& GetLabel() const { return Label.IsEmpty() ? Name : Label; }

		NodePin() = default;
		NodePin(const String& name, EPinDirection direction, EPinCategory category, const String& typeId)
			: Name(name), Direction(direction), Category(category), TypeId(typeId) {}

		// v1 rule: opposite directions, same category, same type id. A pose output only plugs into a
		// pose input; a float data pin only into a float data pin.
		static bool CanConnect(const NodePin& a, const NodePin& b)
		{
			return a.Direction != b.Direction && a.Category == b.Category && a.TypeId == b.TypeId;
		}
	};
}

#endif //PLUENGINE_NODEPIN_H
