//
// Created by Plutex on 7/25/26.
//

#ifndef PLUENGINE_GRAPHEVALCONTEXT_H
#define PLUENGINE_GRAPHEVALCONTEXT_H

#include "PluEngine/Core.h"
#include "PluBase/PluUUID.h"
#include "PluSTL/Array/Array.h"

namespace Plu
{
	struct NodeGraph;

	// Domain-agnostic state threaded through one evaluation of a graph. Not reflected — rebuilt per
	// evaluation by whoever drives the graph, never persisted. Domain contexts derive this and add
	// their own payload (AnimEvalContext: time, skeleton, variable instance).
	//
	// Polymorphic on purpose: the data-pin contract on GraphNode takes this base, so a node that needs
	// its domain's extras downcasts (dynamic_cast) instead of every graph domain re-declaring the
	// contract. A node that only computes values (math/logic/vector) never needs the downcast and
	// therefore works in every graph domain unchanged.
	struct PLUASSETTYPES_API GraphEvalContext
	{
		virtual ~GraphEvalContext() = default;

		// The graph being evaluated. Set by the concrete graph's Evaluate entry point (e.g.
		// AnimationGraph::Evaluate) — used to resolve links to upstream nodes. Never filled by hand.
		NodeGraph* Graph = nullptr;

		// Nodes currently being read through a data pin, innermost last. Data evaluation is pull-based
		// recursion with no topological sort, so a data cycle would recurse forever; GraphNode's data
		// read pushes/pops here and refuses to descend into a node already on the stack.
		DynamicArray<PluUUID> DataEvalStack;
	};
}

#endif //PLUENGINE_GRAPHEVALCONTEXT_H
