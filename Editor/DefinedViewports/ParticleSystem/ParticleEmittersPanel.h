//
// Created by Plutex on 9/26/26.
//

#ifndef PLUENGINE_PARTICLEEMITTERSPANEL_H
#define PLUENGINE_PARTICLEEMITTERSPANEL_H
#include "EditorViewports/IEditorPanel.h"
#include "ParticleEmittersPanel.generated.h"
#include "PluEngine/Core.h"
#include "PluEngine/PluUUID.h"

namespace Plu
{
	// Emitter list: add / rename / delete / reorder / enable. Every mutation bumps CompileRevision and dirties the asset.
	PLU_CLASS()
	class ParticleEmittersPanel : public IEditorPanel
	{
		REFLECTION_BODY_PARTICLEEMITTERSPANEL()
	private:
		PluUUID mRenamingEmitter = PluUUID(0);
		char mRenameBuffer[128] = {};
	public:
		ParticleEmittersPanel() = default;
		~ParticleEmittersPanel() override = default;

		String GetPanelName() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_PARTICLEEMITTERSPANEL_H
