//
// Created by Plutex on 7/21/26.
//

#ifndef PLUENGINE_NODEVIEWREGISTRY_H
#define PLUENGINE_NODEVIEWREGISTRY_H

#include "PluEngine/Core.h"
#include "PluSTL/HashMap/HashMap.h"
#include "PluSTL/String/String.h"
#include "NodeGraph/INodeView.h"

namespace Plu
{
	struct GraphNode;

	// Maps a node's reflected type name to its custom view. Domain code registers views for the node
	// types that want special visuals; everything else uses the shared DefaultNodeView.
	class NodeViewRegistry
	{
		HashMap<String, INodeView*> mViews;
		DefaultNodeView mDefaultView;

	public:
		void Register(const String& typeName, INodeView* view);
		INodeView* For(GraphNode* node);
	};
}

#endif //PLUENGINE_NODEVIEWREGISTRY_H
