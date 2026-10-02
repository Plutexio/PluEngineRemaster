//
// Created by Plutex on 2026-02-18.
//

#include "InputViewerPanel.h"

#include "PluEngine/Core/ApplicationInfo.h"
#include "PluEngine/Core/InputInfo.h"
#include "PluEngine/Core/Reflection/ReflectionBase.h"
#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/Gameplay/InputHandler.h"
#include "PluEngine/Gameplay/InputManager.h"
#include "PluEngine/Timer.h"
#include "UI/IconsFontAwesome7.h"

namespace {
Plu::EnumInfo *KeyEnumInfo() {
  static Plu::EnumInfo *info =
      Plu::TypeRegistry::GetInstance()->GetEnumByT<Key>();
  return info;
}

Plu::EnumInfo *MouseEnumInfo() {
  static Plu::EnumInfo *info =
      Plu::TypeRegistry::GetInstance()->GetEnumByT<MouseButton>();
  return info;
}

const char *KeyName(Key key) {
  if (Plu::EnumInfo *info = KeyEnumInfo()) {
    for (Plu::EnumValue *ev : info->EnumValues) {
      if (ev->Value == static_cast<UInt64>(key))
        return ev->ValueName.CStr();
    }
  }
  return "?";
}

const char *MouseButtonName(MouseButton button) {
  if (Plu::EnumInfo *info = MouseEnumInfo()) {
    for (Plu::EnumValue *ev : info->EnumValues) {
      if (ev->Value == static_cast<UInt64>(button))
        return ev->ValueName.CStr();
    }
  }
  return "?";
}

const char *ButtonStateName(ButtonState state) {
  switch (state) {
  case ButtonState::Released:
    return "Released";
  case ButtonState::Pressed:
    return "Pressed";
  case ButtonState::Held:
    return "Held";
  case ButtonState::JustReleased:
    return "JustReleased";
  }
  return "?";
}

ImVec4 ButtonStateColor(ButtonState state) {
  switch (state) {
  case ButtonState::Released:
    return {0.5f, 0.5f, 0.5f, 1.0f};
  case ButtonState::Pressed:
    return {0.3f, 1.0f, 0.3f, 1.0f};
  case ButtonState::Held:
    return {1.0f, 0.9f, 0.2f, 1.0f};
  case ButtonState::JustReleased:
    return {1.0f, 0.6f, 0.2f, 1.0f};
  }
  return {1.0f, 1.0f, 1.0f, 1.0f};
}

// Draws one key row: name + colored state + a pin/unpin toggle button.
// Returns true if the pin toggle was clicked this frame.
bool DrawKeyRow(Key key, ButtonState state, bool pinned, bool pinnedRow) {
  ImGui::PushID((Plu::ToString(key) + (pinnedRow ? "off" : "pinned")).CStr());

  const bool toggled = ImGui::SmallButton(pinned ? ICON_FA_THUMBTACK_SLASH
                                                 : ICON_FA_THUMBTACK "##off");
  ImGui::SameLine();
  ImGui::Text("%-14s", KeyName(key));
  ImGui::SameLine();
  ImGui::TextColored(ButtonStateColor(state), "%s", ButtonStateName(state));

  ImGui::PopID();
  return toggled;
}

void DrawMouseState(const MouseState &mouse) {
  ImGui::Text("Position : %.1f, %.1f", mouse.x, mouse.y);
  ImGui::Text("Delta    : %.1f, %.1f", mouse.deltaX, mouse.deltaY);
  ImGui::Text("Scroll   : %.2f, %.2f", mouse.scrollX, mouse.scrollY);

  for (int i = 0; i < static_cast<int>(MouseButton::Count); ++i) {
    const auto button = static_cast<MouseButton>(i);
    const ButtonState state = mouse.buttons[i];
    ImGui::Text("%-8s", MouseButtonName(button));
    ImGui::SameLine();
    ImGui::TextColored(ButtonStateColor(state), "%s", ButtonStateName(state));
  }
}
} // namespace

Plu::String Plu::InputViewerPanel::GetPanelName() {
  return ICON_FA_EYE " Input Viewer";
}

void Plu::InputViewerPanel::OnUpdate(float deltaTime) {
  PLU_PROFILE_SCOPE("InputViewerPanel::OnUpdate");
  if (BeginPanel()) {
    InputHandler *handler = nullptr;
    if (InputHandlerOwner)
      handler = InputHandlerOwner->GetInputHandler();

    auto backend = mApplicationInfo->AppInputManager->GetInputBackend();

    // --- Source header ---
    if (handler) {
      ImGui::TextColored({0.3f, 1.0f, 0.3f, 1.0f}, ICON_FA_LINK " Handler: %s",
                         InputHandlerOwner->GetDisplayName().CStr());
    } else {
      ImGui::TextColored({0.6f, 0.6f, 0.6f, 1.0f},
                         ICON_FA_KEYBOARD " Global (InputManager)");
    }
    ImGui::Separator();

    // Resolve current state of a key according to the active source.
    auto keyState = [&](Key k) -> ButtonState {
      if (handler) {
        return handler->mKeyboard.Contains(k) ? handler->mKeyboard[k]
                                              : ButtonState::Released;
      }
      return backend->GetKeyboard().keys[static_cast<int>(k)];
    };

    // --- Pinned keys ---
    if (!mPinnedKeys.IsEmpty()) {
      ImGui::SeparatorText("Pinned");
      // Snapshot iteration is fine; unpin is deferred until after the loop.
      Key toUnpin = Key::Unknown;
      for (Key k : mPinnedKeys) {
        if (DrawKeyRow(k, keyState(k), true, true))
          toUnpin = k;
      }
      if (toUnpin != Key::Unknown)
        mPinnedKeys.Remove(toUnpin);
    }

    // --- Full keyboard list + filter ---
    ImGui::SeparatorText("Keyboard");
    mKeyFilter.Draw(ICON_FA_MAGNIFYING_GLASS " Filter");

    if (EnumInfo *keyEnum = KeyEnumInfo()) {
      for (EnumValue *ev : keyEnum->EnumValues) {
        if (ev->ValueName == "Count" || ev->ValueName == "Unknown")
          continue;
        if (!mKeyFilter.PassFilter(ev->ValueName.CStr()))
          continue;

        const auto k = static_cast<Key>(ev->Value);
        const bool pinned = mPinnedKeys.Contains(k);
        if (DrawKeyRow(k, keyState(k), pinned, false)) {
          if (pinned)
            mPinnedKeys.Remove(k);
          else
            mPinnedKeys.PushBack(k);
        }
      }
    }

    // --- Mouse ---
    ImGui::SeparatorText("Mouse");
    DrawMouseState(handler ? handler->mMouseState : backend->GetMouse());

    // --- Registered bindings (handler only) ---
    if (handler) {
      ImGui::SeparatorText("Registered Bindings");
      for (auto &action : handler->mPressedActions)
        ImGui::BulletText("OnPress   %s", KeyName(action.first));
      for (auto &action : handler->mHoldActions)
        ImGui::BulletText("OnHold    %s", KeyName(action.first));
      for (auto &action : handler->mReleasedActions)
        ImGui::BulletText("OnRelease %s", KeyName(action.first));
      for (auto &action : handler->mMousePressActions)
        ImGui::BulletText("OnMousePress   %s", MouseButtonName(action.first));
      for (auto &action : handler->mMouseReleaseActions)
        ImGui::BulletText("OnMouseRelease %s", MouseButtonName(action.first));
    }
  }
  EndPanel();
}

void Plu::InputViewerPanel::OnHide() {}

void Plu::InputViewerPanel::OnShow() { SetCanClose(true); }
