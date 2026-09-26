//
// Created by Plutex on 9/26/26.
//

#ifndef PLUENGINE_PARTICLEPARAMETERSPANEL_H
#define PLUENGINE_PARTICLEPARAMETERSPANEL_H
#include "EditorViewports/IEditorPanel.h"
#include "ParticleParametersPanel.generated.h"
#include "PluEngine/Core.h"

namespace Plu
{
	// User parameters of the system: add / rename / delete / select (shared by all emitters).
	PLU_CLASS()
	class ParticleParametersPanel : public IEditorPanel
	{
		REFLECTION_BODY_PARTICLEPARAMETERSPANEL()
	private:
		String mRenamingParameter;
		char mRenameBuffer[128] = {};
	public:
		ParticleParametersPanel() = default;
		~ParticleParametersPanel() override = default;

		String GetPanelName() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_PARTICLEPARAMETERSPANEL_H
