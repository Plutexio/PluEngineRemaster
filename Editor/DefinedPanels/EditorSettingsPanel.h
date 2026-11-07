//
// Created by Plutex on 5/3/26.
//

#ifndef PLUENGINE_EDITORSETTINGSPANEL_H
#define PLUENGINE_EDITORSETTINGSPANEL_H
#include "Panels/EditorPanel.h"
#include "PluSTL/Array/Array.h"
#include "PluEngine/Core.h"
#include "PluEngine/Platform/Window.h"
#include "EditorSettingsPanel.generated.h"


namespace Plu
{
    PLU_CLASS()
    class EditorSettingsPanel : public EditorPanel
    {
        REFLECTION_BODY_EDITORSETTINGSPANEL()
    public:
        using EditorPanel::EditorPanel;

        String GetPanelName() override;
        void OnHide() override;
        void OnShow() override;
        void OnUpdate(float deltaTime) override;

    private:
        void DrawDisplaySection();
        // Enumeracja trybów jest droga (WinAPI/SDL zapytanie o monitor) — cache'ujemy i odświeżamy
        // dopiero po zmianie fullscreena, nie co klatkę.
        void RefreshDisplayModes();
        // Spisuje aktualny stan Display okna do EditorSettings i zapisuje na dysk.
        void PersistDisplaySettings();
        // Pola EditorSettings rysowane ręcznie w sekcji Display — pomijane w generycznej pętli.
        static bool IsDisplayProperty(const String& name);

        DynamicArray<DisplayMode> mCachedModes;
        // Unikalne rozdzielczości (bez wariantów odświeżania) — do comba wyboru rozdzielczości.
        DynamicArray<IVec2> mCachedResolutions;
        bool mModesCached = false;
        int mSelectedResolutionIndex = -1;
    };
}



#endif //PLUENGINE_EDITORSETTINGSPANEL_H
