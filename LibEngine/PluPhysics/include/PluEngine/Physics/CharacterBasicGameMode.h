//
// Created by Plutex on 7/20/26.
//

#ifndef PLUENGINE_CHARACTERBASICGAMEMODE_H
#define PLUENGINE_CHARACTERBASICGAMEMODE_H

#include "PluEngine/Gameplay/GameMode.h"
#include "CharacterBasicGameMode.generated.h"

namespace Plu
{
    PLU_CLASS()
    class PLUGAMEPLAY_API CharacterBasicGameMode : public GameMode
    {
        REFLECTION_BODY_CHARACTERBASICGAMEMODE()
    public:
        CharacterBasicGameMode() = default;
        ~CharacterBasicGameMode() = default;

        void OnSetupComponents() override;
    };
}

#endif //PLUENGINE_CHARACTERBASICGAMEMODE_H
