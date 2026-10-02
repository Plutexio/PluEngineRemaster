//
// Created by Plutex on 5/3/26.
//

#include "EditorSettingsPanel.h"

#include <cmath>

#include "EditorSettings/EditorSettings.h"
#include "EditorSettings/EditorSettingsManager.h"
#include "PluEngine/Application.h"
#include "PluEngine/FrameDemand.h"
#include "PluEngine/Platform/Window.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Render/RenderingManager.h"

namespace {
using Plu::String;

Plu::String AspectRatioString(IVec2 res) {
  if (res.x <= 0 || res.y <= 0)
    return {};

  const float ratio = static_cast<float>(res.x) / static_cast<float>(res.y);
  struct Common {
    const char *Name;
    float Value;
  };
  static const Common kCommon[] = {
      {"5:4", 5.0f / 4.0f},     {"4:3", 4.0f / 3.0f},   {"3:2", 3.0f / 2.0f},
      {"16:10", 16.0f / 10.0f}, {"16:9", 16.0f / 9.0f}, {"21:9", 21.0f / 9.0f},
      {"32:9", 32.0f / 9.0f},
  };
  for (const Common &c : kCommon) {
    if (std::abs(ratio - c.Value) <= 0.02f)
      return {c.Name};
  }

  int a = res.x, b = res.y;
  while (b != 0) {
    int t = b;
    b = a % b;
    a = t;
  }
  return String::Format("{0}:{1}", res.x / a, res.y / a);
}
} // namespace

Plu::String Plu::EditorSettingsPanel::GetPanelName() { return "Settings"; }

void Plu::EditorSettingsPanel::OnHide() {}

void Plu::EditorSettingsPanel::OnShow() { SetCanClose(true); }

void Plu::EditorSettingsPanel::RefreshDisplayModes() {
  IWindow *window = mApplicationInfo->AppWindow.GetRaw();

  mCachedModes = window->GetSupportedDisplayModes();

  mCachedResolutions.Clear();
  for (const DisplayMode &mode : mCachedModes) {
    IVec2 res(mode.Width, mode.Height);
    if (mCachedResolutions.IsEmpty() ||
        mCachedResolutions[mCachedResolutions.Size() - 1] != res) {
      mCachedResolutions.PushBack(res);
    }
  }

  EditorSettings *settings =
      EditorSettingsManager::GetInstance()->GetSettings();
  IVec2 wanted(settings->FullscreenWidth, settings->FullscreenHeight);
  if (wanted.x <= 0 || wanted.y <= 0)
    wanted = window->GetDesktopResolution();

  mSelectedResolutionIndex = mCachedResolutions.IsEmpty() ? -1 : 0;
  for (int i = 0; i < static_cast<int>(mCachedResolutions.Size()); ++i) {
    if (mCachedResolutions[i] == wanted) {
      mSelectedResolutionIndex = i;
      break;
    }
  }

  mModesCached = true;
}

void Plu::EditorSettingsPanel::PersistDisplaySettings() {
  IWindow *window = mApplicationInfo->AppWindow.GetRaw();
  EditorSettings *settings =
      EditorSettingsManager::GetInstance()->GetSettings();

  settings->WindowMode = window->GetFullscreenType();
  if (mSelectedResolutionIndex >= 0) {
    settings->FullscreenWidth = mCachedResolutions[mSelectedResolutionIndex].x;
    settings->FullscreenHeight = mCachedResolutions[mSelectedResolutionIndex].y;
  }
  EditorSettingsManager::GetInstance()->Save();
}

bool Plu::EditorSettingsPanel::IsDisplayProperty(const String &name) {
  return name == "VSync" || name == "PowerSaving" || name == "WindowMode" ||
         name == "FullscreenWidth" || name == "FullscreenHeight";
}

void Plu::EditorSettingsPanel::DrawDisplaySection() {
  ImGui::SeparatorText("Display");

  IWindow *window = mApplicationInfo->AppWindow.GetRaw();

  bool vsyncEnabled = window->IsVSyncEnabled();
  if (ImGui::Checkbox("VSync", &vsyncEnabled)) {
    window->SetVSyncEnabled(vsyncEnabled);
    EditorSettingsManager::GetInstance()->GetSettings()->VSync = vsyncEnabled;
    PersistDisplaySettings();
  }

  EditorSettings *settings =
      EditorSettingsManager::GetInstance()->GetSettings();
  if (ImGui::Checkbox("Power Saving", &settings->PowerSaving)) {
    SetPowerSavingEnabled(settings->PowerSaving);
    PersistDisplaySettings();
  }
  ImGui::SetItemTooltip("Redraw the editor only when something changes instead of every frame.\n"
                        "Play mode always runs at full frame rate.");

  if (!mModesCached) {
    RefreshDisplayModes();
  }

  static const char *kFullscreenLabels[] = {"Windowed", "Fullscreen",
                                            "Borderless Window"};
  FullscreenType current = window->GetFullscreenType();
  int currentIdx = static_cast<int>(current);
  if (ImGui::Combo("Window Mode", &currentIdx, kFullscreenLabels,
                   IM_ARRAYSIZE(kFullscreenLabels))) {
    FullscreenType selected = static_cast<FullscreenType>(currentIdx);
    IVec2 resolution(0, 0);
    if (selected == FullscreenType::Fullscreen &&
        mSelectedResolutionIndex >= 0) {
      resolution = mCachedResolutions[mSelectedResolutionIndex];
    }
    window->MakeFullscreen(selected, resolution);
    PersistDisplaySettings();
  }

  ImGui::BeginDisabled(window->GetFullscreenType() !=
                           FullscreenType::Fullscreen ||
                       mCachedResolutions.IsEmpty());
  String preview = String("—");
  if (mSelectedResolutionIndex >= 0) {
    IVec2 res = mCachedResolutions[mSelectedResolutionIndex];
    preview =
        String::Format("{0}x{1}  ({2})", res.x, res.y, AspectRatioString(res));
  }
  if (ImGui::BeginCombo("Resolution", preview.CStr())) {
    for (int i = 0; i < static_cast<int>(mCachedResolutions.Size()); ++i) {
      IVec2 res = mCachedResolutions[i];
      String label = String::Format("{0}x{1}  ({2})", res.x, res.y,
                                    AspectRatioString(res));
      bool isSelected = (i == mSelectedResolutionIndex);
      if (ImGui::Selectable(label.CStr(), isSelected)) {
        mSelectedResolutionIndex = i;
        if (window->GetFullscreenType() == FullscreenType::Fullscreen) {
          window->MakeFullscreen(FullscreenType::Fullscreen,
                                 mCachedResolutions[i]);
        }
        PersistDisplaySettings();
      }
      if (isSelected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
}

void Plu::EditorSettingsPanel::OnUpdate(float deltaTime) {
  if (BeginPanel()) {
    DrawDisplaySection();

    ImGui::SeparatorText("Rendering");
    bool limitTextureLoadPerFrame =
        mApplicationInfo->AppRenderingManager->IsLimitTextureLoadPerFrame();
    if (ImGui::Checkbox("Limit Amount of Textures loaded per frame",
                        &limitTextureLoadPerFrame)) {
      mApplicationInfo->AppRenderingManager->SetLimitTextureLoadPerFrame(
          limitTextureLoadPerFrame);
    }
    ImGui::SetItemTooltip("Purely for fun, I just think it looks cool when "
                          "textures seem to load frame by frame (In runtime "
                          "this will not work :( Performance they said)");

    ImGui::SeparatorText("Editor");
    EditorSettings *settings =
        EditorSettingsManager::GetInstance()->GetSettings();
    for (auto prop : EditorSettings::GetStaticClass()->Properties) {
      if (IsDisplayProperty(prop->PropertyName))
        continue;
      if (prop->EditorControlPtr(prop->GetPtr(settings),
                                 MakeStringForDisplay(prop->PropertyName))) {
        EditorSettingsManager::GetInstance()->Save();
      }
    }
  }
  EndPanel();
}
