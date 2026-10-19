//
// Created by Plutex on 1/14/26.
//

#ifndef PLUENGINE_MATERIALDETAILSPANEL_H
#define PLUENGINE_MATERIALDETAILSPANEL_H
#include "EditorViewports/IEditorPanel.h"
#include "MaterialDetailsPanel.generated.h"

namespace Plu
{
	PLU_CLASS()
	class MaterialDetailsPanel : public IEditorPanel
	{
		REFLECTION_BODY_MATERIALDETAILSPANEL()
	public:
		MaterialDetailsPanel() = default;
		~MaterialDetailsPanel() override = default;

		String GetPanelName() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_MATERIALDETAILSPANEL_H