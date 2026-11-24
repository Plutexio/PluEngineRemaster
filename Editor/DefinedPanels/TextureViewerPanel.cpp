//
// Created by Plutex on 5/4/26.
//

#include "TextureViewerPanel.h"

#include "Panels/EditorPanelManager.h"
#include "PluEngine/PluUtils.h"

Plu::String Plu::TextureViewerPanel::GetPanelName()
{
    return "Texture Viewer";
}

void Plu::TextureViewerPanel::OnHide()
{
}

void Plu::TextureViewerPanel::OnShow()
{
    SetCanClose(true);
    if (!FrameBufferToView && !TextureToView) {
        mEditorPanelManager->ClosePanel(*GetEngineObjectHandle());
    }
}

void Plu::TextureViewerPanel::OnUpdate(float deltaTime)
{
    if (BeginPanel() && (TextureToView || FrameBufferToView)) {
    }
    EndPanel();
}
