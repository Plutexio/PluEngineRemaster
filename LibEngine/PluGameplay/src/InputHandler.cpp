//
// Created by Plutex on 2026-02-27.
//

#include "PluEngine/Gameplay/InputHandler.h"

Plu::InputHandler::~InputHandler()
{
	mHoldActions.Clear();
	mPressedActions.Clear();
	mReleasedActions.Clear();
	mMouseReleaseActions.Clear();
	mMousePressActions.Clear();
	mKeyboard.Clear();
}

void Plu::InputHandler::TickHandler()
{
	for (auto key : mKeyboard) {
		if (key.second != ButtonState::Held) continue;
		if (mHoldActions.Contains(key.first)) {
			try {
				mHoldActions[key.first]();
			} catch (pybind11::error_already_set& e) {
				PLU_CORE_ERROR("Error occurred in Python when calling Key Hold Action. {}", e.what());
			}
		}
	}
}

void Plu::InputHandler::UpdateKeyState(Key key, ButtonState state)
{
	mKeyboard[key] = state;
	if (state == ButtonState::Pressed && mPressedActions.Contains(key)) {
		try {
			mPressedActions[key]();
		} catch (pybind11::error_already_set& e) {
			PLU_CORE_ERROR("Error occurred in Python when calling Key Press Action. {}", e.what());
		}
	}
	if (state == ButtonState::JustReleased && mReleasedActions.Contains(key)) {
		mReleasedActions[key]();
		try {
			mReleasedActions[key]();
		} catch (pybind11::error_already_set& e) {
			PLU_CORE_ERROR("Error occurred in Python when calling Key Release Action. {}", e.what());
		}
	}
}

void Plu::InputHandler::UpdateMouseState(MouseState *mouseState)
{
	mMouseState = *mouseState;
}

void Plu::InputHandler::UpdateMouseKeyState(MouseButton button, ButtonState state)
{
	if (state == ButtonState::Pressed && mMousePressActions.Contains(button)) {
		try
		{
			mMousePressActions[button]();
		} catch (pybind11::error_already_set& e)
		{
			PLU_CORE_ERROR("Error while calling Mouse Press Action. {}", e.what());
		} catch (...)
		{
			PLU_CORE_ERROR("Unknown error while calling Mouse Press Action.");
		}
	}
	if (state == ButtonState::JustReleased && mMouseReleaseActions.Contains(button)) {
		try
		{
			mMouseReleaseActions[button]();
		} catch (pybind11::error_already_set& e)
		{
			PLU_CORE_ERROR("Error while calling Mouse Release Action. {}", e.what());
		} catch (...)
		{
			PLU_CORE_ERROR("Unknown error while calling Mouse Release Action.");
		}
	}
}

void Plu::InputHandler::AddActionOnPress(Key key, std::function<void()> callback)
{
	mPressedActions[key] = callback;
}

void Plu::InputHandler::AddActionOnRelease(Key key, std::function<void()> callback)
{
	mReleasedActions[key] = callback;
}

void Plu::InputHandler::AddActionOnHold(Key key, std::function<void()> callback)
{
	mHoldActions[key] = callback;
}

void Plu::InputHandler::AddActionOnMouseRelease(MouseButton button, std::function<void()> callback)
{
	PLU_CORE_TRACE("New Action On Mouse Release");
	mMouseReleaseActions[button] = callback;
}

void Plu::InputHandler::AddActionOnMousePress(MouseButton button, std::function<void()> callback)
{
	PLU_CORE_TRACE("New Action On Mouse Press");
	mMousePressActions[button] = callback;
}

float Plu::InputHandler::GetMouseDeltaX() const
{
	return mMouseState.deltaX;
}

float Plu::InputHandler::GetMouseDeltaY() const
{
	return mMouseState.deltaY;
}

float Plu::InputHandler::GetMouseScrollWheelDelta() const
{
	return mMouseState.scrollX;
}

float Plu::InputHandler::GetMouseScrollHorizontalWheelDelta() const
{
	return mMouseState.scrollY;
}
