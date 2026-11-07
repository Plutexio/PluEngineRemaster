//
// Created by Plutex on 2026-02-27.
//

#ifndef PLUENGINE_INPUTHANDLER_H
#define PLUENGINE_INPUTHANDLER_H

#include <functional>

#include "PluEngine/Core/InputInfo.h"
#include "PluSTL/PluSTL_FWD.h"
#include "PluEngine/Core.h"

namespace Plu
{
	PLU_CLASS(PyExport, NoReflection)
	class PLUGAMEPLAY_API InputHandler final
	{
		HashMap<Key, std::function<void()>> mPressedActions;
		HashMap<Key, std::function<void()>> mReleasedActions;
		HashMap<Key, std::function<void()>> mHoldActions;
		HashMap<MouseButton, std::function<void()>> mMousePressActions;
		HashMap<MouseButton, std::function<void()>> mMouseReleaseActions;
		HashMap<Key, ButtonState> mKeyboard;
		MouseState mMouseState;

#ifdef PLU_ENGINE_EDITOR_BUILD
		friend class InputViewerPanel;
#endif
	public:
		InputHandler() = default;
		~InputHandler();

		void TickHandler();
		void UpdateKeyState(Key key, ButtonState state);
		void UpdateMouseState(MouseState* mouseState);
		void UpdateMouseKeyState(MouseButton button, ButtonState state);

		PLU_FUNCTION()
		void AddActionOnPress(Key key, std::function<void()> callback);
		PLU_FUNCTION()
		void AddActionOnRelease(Key key, std::function<void()> callback);
		PLU_FUNCTION()
		void AddActionOnHold(Key key, std::function<void()> callback);
		PLU_FUNCTION()
		void AddActionOnMouseRelease(MouseButton button, std::function<void()> callback);
		PLU_FUNCTION()
		void AddActionOnMousePress(MouseButton button, std::function<void()> callback);

		PLU_FUNCTION()
		[[nodiscard]] float GetMouseDeltaX() const;
		PLU_FUNCTION()
		[[nodiscard]] float GetMouseDeltaY() const;
		PLU_FUNCTION()
		[[nodiscard]] float GetMouseScrollWheelDelta() const;
		PLU_FUNCTION()
		[[nodiscard]] float GetMouseScrollHorizontalWheelDelta() const;
	};
}

#endif //PLUENGINE_INPUTHANDLER_H