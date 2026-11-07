//
// Created by Plutex on 1/14/26.
//

#ifndef PLUENGINE_TEXTUREDISPLAYPANEL_H
#define PLUENGINE_TEXTUREDISPLAYPANEL_H
#include "EditorViewports/IEditorPanel.h"
#include "TextureDisplayPanel.generated.h"

namespace Plu
{
	PLU_CLASS()
	class TextureDisplayPanel : public IEditorPanel
	{
		REFLECTION_BODY_TEXTUREDISPLAYPANEL()
	public:
		TextureDisplayPanel() = default;
		~TextureDisplayPanel() override = default;

		String GetPanelName() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_TEXTUREDISPLAYPANEL_H