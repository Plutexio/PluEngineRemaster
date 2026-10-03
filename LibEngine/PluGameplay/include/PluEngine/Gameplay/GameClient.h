//
// Created by Plutex on 2026-02-12.
//

#ifndef PLUENGINE_GAMECLIENT_H
#define PLUENGINE_GAMECLIENT_H
#include "PluEngine/Core.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "GameClient.generated.h"

namespace Plu
{
	class IWindow;
	class InputManager;
	PLU_CLASS(Abstract)
	class PLUGAMEPLAY_API GameClient : public EngineObject
	{
		REFLECTION_BODY_GAMECLIENT()
	private:
		DynamicArray<TOwningPointer<class GameLocalPlayer>> mLocalPlayers;
		TUsePointer<EngineObjectManager> mObjectManager;
		TUsePointer<SceneManager> mScenesManager;
		TUsePointer<InputManager> mInputManager;
		TUsePointer<IWindow> mWindow;

		friend class SceneWorld;

		bool mShowCursor = false;
		Int32 mNewPlayerEvent = 0;
	public:
		GameClient(const TUsePointer<EngineObjectManager> &objectManager, const TUsePointer<SceneManager> &scenesManager, const TUsePointer<InputManager> &inputManager, TUsePointer<IWindow> window);
		~GameClient() override;

		void ExitGame();

		[[nodiscard]] bool IsCursorShown() const;
		void HideCursor();
		void ShowCursor();

		//Returns UInt16 as player ID, by this id you can get player controller, puppet and every object associated with that player
		//For now we only support one player
		TUsePointer<GameLocalPlayer> GetLocalPlayerByID(UInt16 id);
		UInt16 JoinGameLocally();
	};

    // The active client. Lives here rather than beside Application because everything that asks
    // for it is gameplay code, and gameplay sits below the application layer. Application sets it
    // when a game starts and clears it when one ends.
    PLUGAMEPLAY_API TUsePointer<GameClient> GetGameClient();
    PLUGAMEPLAY_API void SetGameClient(TUsePointer<GameClient> client);
}

#endif //PLUENGINE_GAMECLIENT_H
