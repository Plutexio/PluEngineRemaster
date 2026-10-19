//
// Created by Plutex on 1/13/26.
//

#ifndef PLUENGINE_TEXTUREVIEWPORT_H
#define PLUENGINE_TEXTUREVIEWPORT_H
#include "EditorViewports/IEditorViewport.h"
#include "TextureViewport.generated.h"

namespace Plu
{
	PLU_CLASS()
	class TextureViewport : public IEditorViewport
	{
		REFLECTION_BODY_TEXTUREVIEWPORT()
	public:
		TextureViewport() = default;
		~TextureViewport() override = default;

		void OnClosed() override;
		void OnOpened() override;
		void OnPanelRegister() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_TEXTUREVIEWPORT_H