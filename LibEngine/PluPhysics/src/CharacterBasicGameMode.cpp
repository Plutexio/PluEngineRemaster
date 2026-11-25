//
// Created by Plutex on 7/20/26.
//

#include "PluEngine/Physics/CharacterBasicGameMode.h"

#include "PluEngine/Physics/CharacterPuppet.h"

void Plu::CharacterBasicGameMode::OnSetupComponents()
{
    GameMode::OnSetupComponents();
    PuppetClass = CharacterPuppet::GetStaticClass();
}
