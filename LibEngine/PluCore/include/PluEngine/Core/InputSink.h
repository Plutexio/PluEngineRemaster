//
// Created by Plutex on 8/12/26.
//

#ifndef PLUENGINE_INPUTSINK_H
#define PLUENGINE_INPUTSINK_H
#include <functional>

#include "PluEngine/Core.h"
#include "PluEngine/Core/InputInfo.h"

namespace Plu
{
    // Where a platform input backend delivers what it read from the OS.
    //
    // The backends live in the platform layer, below gameplay, so they must not know that a player
    // exists. InputManager fills these in with calls onto the active local player; until it does,
    // every callback is empty and a backend simply drops the notification.
    struct InputSink
    {
        std::function<void(Key, ButtonState)> OnKeyboardKey;
        std::function<void(MouseButton, ButtonState)> OnMouseKey;
        std::function<void(MouseState&)> OnMouse;

        // True once gameplay has wired the sink up — backends check this before notifying.
        explicit operator bool() const { return static_cast<bool>(OnKeyboardKey); }
    };
}

#endif //PLUENGINE_INPUTSINK_H
