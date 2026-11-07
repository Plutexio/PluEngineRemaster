//
// Created by Plutex on 9/26/26.
//

#ifndef PLUENGINE_PARTICLECURVEPARAMNODEVIEW_H
#define PLUENGINE_PARTICLECURVEPARAMNODEVIEW_H

#include "ParticleModuleNodeView.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleUpdateModules.h"
#include "PluEngine/AssetTypes/Curves/Curve.h"

namespace Plu
{
	// Module frame with a 60x24 thumbnail of the module's curve / gradient under its pins, so an
	// over-life module reads at a glance on the canvas. Editing happens in the Details panel.
	class ParticleCurveParamNodeView : public ParticleModuleNodeView
	{
	protected:
		void DrawBody(GraphNode* node, NodeGraphEditor& editor) override
		{
			ImGui::Dummy(ImVec2(0.0f, 2.0f));
			if (auto* color = dynamic_cast<ColorOverLifeModule*>(node)) {
				DrawGradientThumbnail(color->Gradient, 60.0f, 24.0f);
			} else if (auto* size = dynamic_cast<SizeOverLifeModule*>(node)) {
				DrawCurveThumbnail(size->Scale, 60.0f, 24.0f);
			} else if (auto* speed = dynamic_cast<SizeBySpeedModule*>(node)) {
				DrawCurveThumbnail(speed->Scale, 60.0f, 24.0f);
			}
		}
	};
}

#endif //PLUENGINE_PARTICLECURVEPARAMNODEVIEW_H
