//
// Created by Plutex on 2026-06-26.
//

#ifndef PLUENGINE_RENDERGPUSTATSPANEL_H
#define PLUENGINE_RENDERGPUSTATSPANEL_H
#include "Panels/EditorPanel.h"
#include "RenderGpuStatsPanel.generated.h"

namespace Plu
{
	PLU_CLASS()
	class RenderGpuStatsPanel : public EditorPanel
	{
		REFLECTION_BODY_RENDERGPUSTATSPANEL()
	public:
		using EditorPanel::EditorPanel;

		String GetPanelName() override;
		void OnUpdate(float deltaTime) override;
		void OnHide() override;
		void OnShow() override;
	};
}

#endif //PLUENGINE_RENDERGPUSTATSPANEL_H
