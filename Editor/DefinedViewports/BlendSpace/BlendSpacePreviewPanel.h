//
// Created by Plutex on 10/14/26.
//

#ifndef PLUENGINE_BLENDSPACEPREVIEWPANEL_H
#define PLUENGINE_BLENDSPACEPREVIEWPANEL_H
#include "EditorViewports/IEditorPanel.h"
#include "BlendSpacePreviewPanel.generated.h"
#include "PluEngine/Core.h"

namespace Plu
{
	// 3D preview of a BlendSpace: the asset's PreviewMesh in the overlay scene, driven by the
	// viewport's transient preview graph, shown through the shared editor camera exactly like
	// SkeletalMeshViewportPanel (same FBO path, same navigation, same PIE limitation).
	PLU_CLASS()
	class BlendSpacePreviewPanel : public IEditorPanel
	{
		REFLECTION_BODY_BLENDSPACEPREVIEWPANEL()
	public:
		BlendSpacePreviewPanel() = default;
		~BlendSpacePreviewPanel() override = default;

		String GetPanelName() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnUpdate(float deltaTime) override;

	private:
		UInt32 mSeenRevision = 0; // BlendSpace::GetRevision() the preview last showed
	};
}

#endif //PLUENGINE_BLENDSPACEPREVIEWPANEL_H
