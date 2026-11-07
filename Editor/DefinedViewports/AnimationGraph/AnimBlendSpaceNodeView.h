//
// Created by Plutex on 10/14/26.
//

#ifndef PLUENGINE_ANIMBLENDSPACENODEVIEW_H
#define PLUENGINE_ANIMBLENDSPACENODEVIEW_H

#include "NodeGraph/INodeView.h"
#include "NodeGraph/NodeGraphEditor.h"
#include "DefinedViewports/BlendSpace/BlendSpaceDrawing.h"
#include "PluEngine/AssetTypes/AnimationGraph/Nodes/AnimBlendSpaceNode.h"
#include "imgui.h"

namespace Plu
{
	// AnimBlendSpaceNode with a thumbnail of its blend space (samples + triangulation) under the
	// pins, so a graph reads at a glance which space a node plays. Pin labels (axis names) come from
	// the node itself (NodePin::Label).
	class AnimBlendSpaceNodeView : public DefaultNodeView
	{
	protected:
		void DrawBody(GraphNode* node, NodeGraphEditor& editor) override
		{
			auto* blendSpaceNode = static_cast<AnimBlendSpaceNode*>(node);
			ImGui::Dummy(ImVec2(0.0f, 2.0f));
			if (blendSpaceNode->BlendSpaceAsset) {
				const BlendSpace& space = *blendSpaceNode->BlendSpaceAsset;
				DrawBlendSpaceThumbnail(space, 120.0f, space.IsTwoD() ? 80.0f : 24.0f);
			} else {
				ImGui::TextDisabled("No blend space");
			}
		}

		ImVec4 HeaderColor(GraphNode* node) override { return ImVec4(0.20f, 0.42f, 0.36f, 1.0f); }
	};
}

#endif //PLUENGINE_ANIMBLENDSPACENODEVIEW_H
