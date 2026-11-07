//
// Created by Plutex on 1/13/26.
//

#ifndef PLUENGINE_MATERIALVIEWPORT_H
#define PLUENGINE_MATERIALVIEWPORT_H
#include "EditorViewports/IEditorViewport.h"
#include "MaterialViewport.generated.h"

namespace Plu
{
	PLU_CLASS()
	class MaterialInfoViewport : public IEditorViewport
	{
		REFLECTION_BODY_MATERIALINFOVIEWPORT()
	public:
		MaterialInfoViewport() = default;
		~MaterialInfoViewport() override = default;

		void OnClosed() override;
		void OnOpened() override;
		void OnPanelRegister() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_MATERIALVIEWPORT_H