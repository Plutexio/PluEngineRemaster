//
// Created by Plutex on 5/3/26.
//

#ifndef PLUENGINE_EDITORSETTINGS_H
#define PLUENGINE_EDITORSETTINGS_H

#include "PluEngine/Core.h"
#include "PluEngine/Platform/Window.h"
#include "EditorSettings.generated.h"

namespace Plu
{
    PLU_STRUCT()
    struct EditorSettings
    {
        REFLECTION_BODY_EDITORSETTINGS()
    public:
        PLU_PROPERTY()
        float EditorCameraScrollWheelSpeedMultiplier = 1.0f;
        PLU_PROPERTY()
        float EditorCameraPanSpeedMultiplier = 1.0f;
        PLU_PROPERTY()
        float EditorCameraMoveSpeedMultiplier = 1.0f;
        PLU_PROPERTY()
        float EditorCameraLookSpeedMultiplier = 1.0f;

        // Ustawienia Display. Rysowane ręcznie w EditorSettingsPanel (potrzebują żywej listy
        // trybów okna), więc panel pomija je w generycznej pętli właściwości — patrz
        // EditorSettingsPanel::IsDisplayProperty.
        PLU_PROPERTY()
        bool VSync = true;
        // Frames on demand while the editor sits idle (FrameDemand.h). Never applies in PIE.
        PLU_PROPERTY()
        bool PowerSaving = true;
        PLU_PROPERTY()
        FullscreenType WindowMode = FullscreenType::Windowed;
        // Rozdzielczość dla WindowMode == Fullscreen. {0,0} = natywna rozdzielczość pulpitu.
        PLU_PROPERTY()
        int FullscreenWidth = 0;
        PLU_PROPERTY()
        int FullscreenHeight = 0;
    };
}

#endif //PLUENGINE_EDITORSETTINGS_H
