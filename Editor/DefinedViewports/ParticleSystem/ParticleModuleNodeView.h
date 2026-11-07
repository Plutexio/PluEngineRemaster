//
// Created by Plutex on 9/26/26.
//

#ifndef PLUENGINE_PARTICLEMODULENODEVIEW_H
#define PLUENGINE_PARTICLEMODULENODEVIEW_H

#include "NodeGraph/INodeView.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleModuleNode.h"
#include "imgui.h"

namespace Plu
{
	// Default node frame with the header tinted by the module's stage: spawn green, update blue,
	// renderer orange, output red; pure data sources (no Flow pins) grey.
	class ParticleModuleNodeView : public DefaultNodeView
	{
	protected:
		ImVec4 HeaderColor(GraphNode* node) override
		{
			auto* module = static_cast<ParticleModuleNode*>(node);
			switch (module->GetStage()) {
				case EParticleModuleStage::Spawn:    return ImVec4(0.18f, 0.48f, 0.26f, 1.0f);
				case EParticleModuleStage::Renderer: return ImVec4(0.72f, 0.42f, 0.12f, 1.0f);
				case EParticleModuleStage::Output:   return ImVec4(0.62f, 0.20f, 0.20f, 1.0f);
				case EParticleModuleStage::Update: break;
			}
			for (const NodePin& pin : node->InputPins) {
				if (pin.Category == EPinCategory::Flow) return ImVec4(0.16f, 0.34f, 0.58f, 1.0f);
			}
			return ImVec4(0.32f, 0.32f, 0.36f, 1.0f);
		}
	};
}

#endif //PLUENGINE_PARTICLEMODULENODEVIEW_H
