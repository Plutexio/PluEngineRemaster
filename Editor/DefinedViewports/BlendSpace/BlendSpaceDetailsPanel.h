//
// Created by Plutex on 10/14/26.
//

#ifndef PLUENGINE_BLENDSPACEDETAILSPANEL_H
#define PLUENGINE_BLENDSPACEDETAILSPANEL_H
#include "EditorViewports/IEditorPanel.h"
#include "BlendSpaceDetailsPanel.generated.h"
#include "PluEngine/Core.h"

namespace Plu
{
	// Asset-level settings (skeleton, preview mesh, dimensions, axes), the sample list, and the
	// selected sample's fields. Every edit dirties the asset and rebuilds its triangulation.
	PLU_CLASS()
	class BlendSpaceDetailsPanel : public IEditorPanel
	{
		REFLECTION_BODY_BLENDSPACEDETAILSPANEL()
	public:
		BlendSpaceDetailsPanel() = default;
		~BlendSpaceDetailsPanel() override = default;

		String GetPanelName() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_BLENDSPACEDETAILSPANEL_H
