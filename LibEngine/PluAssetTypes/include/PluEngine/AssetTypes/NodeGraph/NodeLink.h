//
// Created by Plutex on 7/21/26.
//

#ifndef PLUENGINE_NODELINK_H
#define PLUENGINE_NODELINK_H

#include "PluEngine/Core.h"
#include "PluBase/PluUUID.h"
#include "PluSTL/String/String.h"

namespace Plu
{
	// A connection between two nodes, persisted by identity (node Uuid + pin name), never by the
	// editor's ephemeral integer ids. Direction convention: From = output side, To = input side.
	struct PLUASSETTYPES_API NodeLink
	{
		PluUUID FromNode;
		String  FromPin;
		PluUUID ToNode;
		String  ToPin;

		bool operator==(const NodeLink& other) const
		{
			return FromNode == other.FromNode && ToNode == other.ToNode
				&& FromPin == other.FromPin && ToPin == other.ToPin;
		}
	};
}

#endif //PLUENGINE_NODELINK_H
