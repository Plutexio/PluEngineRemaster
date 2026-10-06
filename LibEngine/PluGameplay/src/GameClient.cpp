//
// Created by Plutex on 2026-02-12.
//

#include "PluEngine/Gameplay/GameClient.h"
#include "PluEngine/Gameplay/GameLocalPlayer.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "PluEngine/Platform/Window.h"
#include "PluEngine/Gameplay/GameLocalPlayer.h"

Plu::GameClient::GameClient(const TUsePointer<EngineObjectManager> &objectManager,
                            const TUsePointer<SceneManager> &scenesManager, const TUsePointer<InputManager> &inputManager, TUsePointer<IWindow> window)
{
	mObjectManager = objectManager;
	mInputManager = inputManager;
	mScenesManager = scenesManager;
	mWindow = window;

	mNewPlayerEvent = mScenesManager->SubscribeToEvent("NewWorld", [&](void*) {
		if (mScenesManager->GetCurrentWorldName() == "Overlay") return;
		for (const auto& localPlayer : mLocalPlayers) {
			localPlayer->JoinPlayerToWorld();
		}
	});
}

Plu::GameClient::~GameClient()
{
	ExitGame();
	mScenesManager->UnsubscribeFromEvent("NewWorld", mNewPlayerEvent);
}

void Plu::GameClient::ExitGame()
{
	for (const auto& localPlayer : mLocalPlayers) {
		mObjectManager->DestroyObject(*localPlayer->GetEngineObjectHandle());
	}
	mLocalPlayers.Clear();
}

bool Plu::GameClient::IsCursorShown() const
{
	return mShowCursor;
}

void Plu::GameClient::HideCursor()
{
	mShowCursor = false;
	mWindow->SetCursorVisibility(false);
}

void Plu::GameClient::ShowCursor()
{
	mShowCursor = true;
	mWindow->SetCursorVisibility(true);
}

Plu::TUsePointer<Plu::GameLocalPlayer> Plu::GameClient::GetLocalPlayerByID(UInt16 id)
{
	return id < mLocalPlayers.Size() ? mLocalPlayers[id] : nullptr;
}

UInt16 Plu::GameClient::JoinGameLocally()
{
	EngineObjectHandle newPlayerHandle = mObjectManager->CreateObject<GameLocalPlayer>();
	TOwningPointer<GameLocalPlayer> newPlayer = mObjectManager->GetObjectAsOwner<GameLocalPlayer>(newPlayerHandle);
	UInt16 idx = mLocalPlayers.Size();
	newPlayer->Init(mScenesManager, idx);
	mLocalPlayers.PushBack(newPlayer);
	newPlayer->JoinPlayerToWorld();
	PLU_CORE_INFO("New player {}", idx);
	return idx;
}

namespace
{
    Plu::TUsePointer<Plu::GameClient> gGameClient;
}

Plu::TUsePointer<Plu::GameClient> Plu::GetGameClient()
{
    return gGameClient;
}

void Plu::SetGameClient(TUsePointer<GameClient> client)
{
    gGameClient = client;
}
