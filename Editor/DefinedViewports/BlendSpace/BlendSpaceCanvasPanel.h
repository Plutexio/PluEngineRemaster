//
// Created by Plutex on 10/14/26.
//

#ifndef PLUENGINE_BLENDSPACECANVASPANEL_H
#define PLUENGINE_BLENDSPACECANVASPANEL_H
#include "EditorViewports/IEditorPanel.h"
#include "BlendSpaceCanvasPanel.generated.h"
#include "PluEngine/Core.h"

namespace Plu
{
	// The BlendSpace sample canvas: axis grid, triangulation, samples and the preview cursor.
	//   - drag a sample to move it (snaps to the axis grid; hold Shift for free placement),
	//   - click / drag on empty space (or Ctrl+drag anywhere) to move the preview cursor,
	//   - right-click empty space to add a sample, right-click a sample for its menu.
	// Sample edits dirty the asset; the preview cursor and selection are view state.
	PLU_CLASS()
	class BlendSpaceCanvasPanel : public IEditorPanel
	{
		REFLECTION_BODY_BLENDSPACECANVASPANEL()
	public:
		BlendSpaceCanvasPanel() = default;
		~BlendSpaceCanvasPanel() override = default;

		String GetPanelName() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnUpdate(float deltaTime) override;

	private:
		enum class EDragMode : UInt8 { None, Sample, Preview };
		EDragMode mDragMode = EDragMode::None;
		Int64 mContextSample = -1; // sample the right-click menu is open for
		Vec2 mPendingAddPosition = Vec2(0.0f);
	};
}

#endif //PLUENGINE_BLENDSPACECANVASPANEL_H
