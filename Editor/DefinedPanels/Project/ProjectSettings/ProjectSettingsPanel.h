//
// Created by Plutex on 5/30/26.
//

#ifndef PLUENGINE_PROJECTSETTINGSPANEL_H
#define PLUENGINE_PROJECTSETTINGSPANEL_H

#include "Panels/EditorPanel.h"
#include "ProjectSettingsPanel.generated.h"

namespace Plu
{
    PLU_CLASS()
    class ProjectSettingsPanel : public EditorPanel
    {
        REFLECTION_BODY_PROJECTSETTINGSPANEL()
    public:
        using EditorPanel::EditorPanel;

        String GetPanelName() override;
        void OnHide() override;
        void OnShow() override;
        void OnUpdate(float deltaTime) override;

    private:
        void DrawPhysicsChannels();

        char mNewChannelName[64] = {};
        int  mNewChannelResponse = 2; // PhysicsCollisionResponse::Block
    };
}



#endif //PLUENGINE_PROJECTSETTINGSPANEL_H
