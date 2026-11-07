//
// Created by Plutex on 2026-02-12.
//

#include "PluEngine/Gameplay/GameLocalPlayer.h"

#include "PluEngine/Gameplay/Controller.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"

void Plu::GameLocalPlayer::JoinPlayerToWorld()
{
	mScenesManager->GetCurrentWorld()->JoinPlayerLocally(mLocalPlayerIndex);
}

void Plu::GameLocalPlayer::Init(const TUsePointer<SceneManager> &sceneManager, UInt16 id)
{
	mScenesManager = sceneManager;
	mLocalPlayerIndex = id;
}

void Plu::GameLocalPlayer::OnKeyboardKeyUpdate(Key key, ButtonState state)
{
	TUsePointer<Controller> controller = mScenesManager->GetCurrentWorld()->GetControllerByID(mLocalPlayerIndex);
	InputHandler* controllerInputHandler = controller->GetInputHandler();
	controllerInputHandler->UpdateKeyState(key, state);
	TUsePointer<Puppet> puppet = controller->GetControllerPuppet();
	if (!puppet) return;
	puppet->GetInputHandler()->UpdateKeyState(key, state);
}

void Plu::GameLocalPlayer::OnMouseKeyUpdate(MouseButton button, ButtonState state)
{
	TUsePointer<Controller> controller = mScenesManager->GetCurrentWorld()->GetControllerByID(mLocalPlayerIndex);
	InputHandler* controllerInputHandler = controller->GetInputHandler();
	controllerInputHandler->UpdateMouseKeyState(button, state);
	TUsePointer<Puppet> puppet = controller->GetControllerPuppet();
	if (!puppet) return;
	puppet->GetInputHandler()->UpdateMouseKeyState(button, state);
}

void Plu::GameLocalPlayer::OnMouseUpdate(MouseState &newState)
{
	TUsePointer<Controller> controller = mScenesManager->GetCurrentWorld()->GetControllerByID(mLocalPlayerIndex);
	InputHandler* controllerInputHandler = controller->GetInputHandler();
	controllerInputHandler->UpdateMouseState(&newState);
	TUsePointer<Puppet> puppet = controller->GetControllerPuppet();
	if (!puppet) return;
	puppet->GetInputHandler()->UpdateMouseState(&newState);
}
