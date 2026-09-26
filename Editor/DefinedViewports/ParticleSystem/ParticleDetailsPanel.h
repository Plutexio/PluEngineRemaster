//
// Created by Plutex on 9/26/26.
//

#ifndef PLUENGINE_PARTICLEDETAILSPANEL_H
#define PLUENGINE_PARTICLEDETAILSPANEL_H
#include "EditorViewports/IEditorPanel.h"
#include "ParticleDetailsPanel.generated.h"
#include "PluEngine/Core.h"

namespace Plu
{
	// Reflected properties of the selection: parameter, parameter node, node, emitter or the system (in that priority).
	PLU_CLASS()
	class ParticleDetailsPanel : public IEditorPanel
	{
		REFLECTION_BODY_PARTICLEDETAILSPANEL()
	public:
		ParticleDetailsPanel() = default;
		~ParticleDetailsPanel() override = default;

		String GetPanelName() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_PARTICLEDETAILSPANEL_H
