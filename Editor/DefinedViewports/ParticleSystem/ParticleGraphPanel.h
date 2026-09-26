//
// Created by Plutex on 9/26/26.
//

#ifndef PLUENGINE_PARTICLEGRAPHPANEL_H
#define PLUENGINE_PARTICLEGRAPHPANEL_H
#include "EditorViewports/IEditorPanel.h"
#include "ParticleGraphPanel.generated.h"
#include "PluEngine/Core.h"

namespace Plu
{
	// Node canvas of the selected emitter, driven by the shared NodeGraphEditor.
	PLU_CLASS()
	class ParticleGraphPanel : public IEditorPanel
	{
		REFLECTION_BODY_PARTICLEGRAPHPANEL()
	public:
		ParticleGraphPanel() = default;
		~ParticleGraphPanel() override = default;

		String GetPanelName() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_PARTICLEGRAPHPANEL_H
