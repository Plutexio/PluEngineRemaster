//
// Created by Plutex on 2026-02-20.
//

#ifndef PLUENGINE_EDITORINTERFACE_H
#define PLUENGINE_EDITORINTERFACE_H
#include <filesystem>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "nfd.h"
#include "DefinedPanels/EngineClassTreePanel.h"
#include "DefinedPanels/EngineStatsPanel.h"
#include "DefinedPanels/ProfilerPanel.h"
#include "DefinedPanels/LoadedShadersPanel.h"
#include "DefinedPanels/LoadedAssetsPanel.h"
#include "DefinedPanels/LoadedObjectsPanel.h"
#include "DefinedPanels/RenderGpuStatsPanel.h"
#include "DefinedPanels/ParticlesDebugPanel.h"
#include "DefinedPanels/Style/EditorStylePanel.h"
#include "Managers/Project/EditorProjectManager.h"
#include "PluEngine/PluPaths.h"
#include "PluEngine/Core/DiskManager.h"
#include "UI/IconsFontAwesome7.h"
#include "Managers/Python/EditorPythonManager.h"
#include "Panels/EditorPanelManager.h"
#include "PluEngine/Gameplay/GameClient.h"
#include "EditorApp.h"
#include "PluEngine/Platform/Window.h"
#include "EditorAppContext.h"
#include "DefinedPanels/EditorSettingsPanel.h"
#include "DefinedPanels/Project/AssetBrowserPanel/AssetBrowserPanel.h"
#include "DefinedPanels/Project/ProjectSettings/ProjectSettingsPanel.h"
#include "Managers/Scene/EditorCamera.h"
#include "Managers/Shaders/EditorShaderManager.h"
#include "PluEngine/PluUtils.h"
#include "nlohmann/json.hpp"
#include "nlohmann/json_fwd.hpp"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/Gameplay/InputManager.h"
#include "PluEngine/Core/IAssetData.h"
#include "PluEngine/Render/RenderingManager.h"
#include "PluEngine/Platform/Window.h"
#include "PluEngine/Platform/WindowsManager.h"
#include "EditorWindows/EditorWindowsManager.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"

extern Plu::TUsePointer<Plu::EngineObjectManager> gEngineObjectManager;
extern Plu::EditorAppContext* gEditorAppContext;
extern Plu::ApplicationInfo* gApplicationInfo;
extern Plu::PluEditor* gPluEditor;

namespace Plu
{
    // Minimize / maximize / close cluster of a window's title bar. Lives here because two places
    // need it: the main toolbar and SinglePanel windows, which have no toolbar at all.
    inline void DrawWindowControls(const TUsePointer<IWindow>& window, ImVec2 buttonDimensions)
    {
        if (!window) return;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.3));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.8));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1,0,0,0));
        if (ImGui::Button(ICON_FA_MINUS "", buttonDimensions))
        {
            window->Minimize();
        }
        if (ImGui::Button(ICON_FA_EXPAND "", buttonDimensions))
        {
            window->Maximize();
        }
        ImGui::PopStyleColor(3);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5,0,0,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,0,0,1));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1,0,0,0));
        if (ImGui::Button(ICON_FA_XMARK "", buttonDimensions))
        {
            // Closing window 0 means quitting, which has to run the unsaved-assets confirmation;
            // a secondary window just goes away and gives its contents back to window 0.
            if (window->GetWindowID() == 0) {
                gPluEditor->OnRequestedWindowClose(window);
            } else {
                gEditorAppContext->EditorWindowsManager->CloseEditorWindow(window->GetWindowID());
            }
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
    }

    inline float DrawToolbarWindow(float toolbarHeight, int windowID)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);

        // Wymuszamy wysokość, ale musimy zadbać o to, by padding okna nie dodawał pustego miejsca
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, toolbarHeight));
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        // KLUCZ: Ustaw WindowPadding na 0, aby okno nie było większe niż pasek menu
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        // Opcjonalnie: dostosuj FramePadding, aby precyzyjnie kontrolować wysokość wnętrza menu
        // Wysokość menu = FontSize + (FramePadding.y * 2)
        float targetFramePaddingY = (toolbarHeight - ImGui::GetFontSize()) / 2.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, targetFramePaddingY));

        // style.WindowMinSize is DPI-scaled by ScaleAllSizes (e.g. 32 -> 48 at 1.5x). At small font
        // sizes our requested toolbarHeight drops below it, so ImGui would clamp the toolbar window
        // taller than intended. Drop the minimum for this window - its size is set explicitly above.
        // Only needed during Begin() (that's where the clamp happens), so pop right after.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
        ImGui::Begin("Toolbar", nullptr, flags);
        ImGui::PopStyleVar();
        ImVec2 sizeForPlayButton = ImGui::GetContentRegionAvail();
        // The Toolbar window itself needs WindowPadding(0,0) (see above), but that value is still on
        // the style stack and would otherwise leak into every popup window opened from here (BeginMenu
        // dropdowns, the play button's context menu) - gluing their text/items to the left edge. Give
        // popups their own padding, popped once the menu bar (and everything opened from it) is done.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 0.55f, ImGui::GetFontSize() * 0.35f));
        ImGui::BeginMenuBar();
        // Extra breathing room between the top-level menu entries (Project/View/Scripts/Build).
        // Scoped to just these - popped before the play button / window controls so those stay tight.
        ImVec2 menuBarSpacing = ImGui::GetStyle().ItemSpacing;
        menuBarSpacing.x += ImGui::GetFontSize() * 0.6f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, menuBarSpacing);
        if (ImGui::BeginMenu("Project"))
        {
            if (ImGui::MenuItem("New Project")) {
                gEditorAppContext->NewProjectPopup = true;
            }
            if (ImGui::MenuItem("Open Project")) {
                nfdu8char_t* outPath = nullptr;
                const nfdu8filteritem_t filters[1] = { { "Plu Project", "pluproject" } };
                if (NFD_OpenDialogU8(&outPath, filters, 1, nullptr) == NFD_OKAY) {
                    gEditorAppContext->EditorProjectManager->OpenProject(StringW::FromNarrow(outPath));
                    NFD_FreePathU8(outPath);
                }
            }
            if (ImGui::BeginMenu("Recent Projects")) {
                if (!std::filesystem::exists(EditorProjectManager::GetRecentProjectsJSONPath().CStr())) {
                    ImGui::Text("No recent Projects!");
                } else {
                    nlohmann::json json = DiskManager::LoadJson(EditorProjectManager::GetRecentProjectsJSONPath());
                    for (const auto& project : json["projects"]) {
                        Path projectPath = project.get<std::string>().c_str();
                        if (ImGui::Selectable(projectPath.GetStem().CStr())) {
                            gEditorAppContext->EditorProjectManager->OpenProject(StringW::FromNarrow(projectPath.CStr()));
                        }
                    }
                }
                ImGui::EndMenu();
            }
            if (gEditorAppContext->EditorProjectManager->IsAnyProjectOpen()) {
                ImGui::Separator();
                bool disabled = gEditorAppContext->EditorProjectManager->GetProjectFileVersion() < 0.1;
                ImGui::BeginDisabled(disabled);
                if (ImGui::MenuItem("Project Settings")) {
                    gEditorAppContext->EditorPanelManager->AddPanel(ProjectSettingsPanel::GetStaticClass());
                }
                if (disabled) {
                    ImGui::SetItemTooltip("Outdated project file! Settings not supported!");
                }
                ImGui::EndDisabled();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Editor Style")) {
                gEditorAppContext->EditorPanelManager->AddPanel(EditorStylePanel::GetStaticClass());
            }
            if (ImGui::MenuItem("Editor Settings")) {
                gEditorAppContext->EditorPanelManager->AddPanel<EditorSettingsPanel>();
            }
            if (ImGui::MenuItem("Profiler")) {
                gEditorAppContext->EditorPanelManager->AddPanel<ProfilerPanel>();
            }
            if (ImGui::MenuItem("Asset Browser")) {
                gEditorAppContext->EditorPanelManager->AddPanel<AssetBrowserPanel>();
            }
            if (ImGui::BeginMenu(ICON_FA_BUG " Debug")) {
                if (ImGui::MenuItem("Loaded Shaders")) {
                    gEditorAppContext->EditorPanelManager->AddPanel<LoadedShadersPanel>();
                }
                if (ImGui::MenuItem("Loaded Assets")) {
                    gEditorAppContext->EditorPanelManager->AddPanel<LoadedAssetsPanel>();
                }
                if (ImGui::MenuItem("Loaded Objects")) {
                    gEditorAppContext->EditorPanelManager->AddPanel<LoadedObjectsPanel>();
                }
                if (ImGui::MenuItem("Render / GPU")) {
                    gEditorAppContext->EditorPanelManager->AddPanel<RenderGpuStatsPanel>();
                }
                if (ImGui::MenuItem("Debug Particles")) {
                    gEditorAppContext->EditorPanelManager->AddPanel<ParticlesDebugPanel>();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Open Any Panel"))
            {
                static DynamicArray<TypeInfo*> panelTypes;
                if (panelTypes.IsEmpty()) {
                    for (auto type : *TypeRegistry::GetInstance()->GetTypeMap()) {
                        if (type.second->IsDerivedOf(EditorPanel::GetStaticClass())) {
                            panelTypes.PushBack(type.second);
                        }
                    }
                }
                for (auto type : panelTypes) {
                    if (ImGui::MenuItem(type->TypeName.CStr()))
                    {
                        gEditorAppContext->EditorPanelManager->AddPanel(type);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        static bool triedLoadingPythonScriptDirs = false;
        static PathW workDir = L"SELECT WORKDIR!";
        static PathW scriptPath = L"SELECT SCRIPT!";
        static String args;
        if (!triedLoadingPythonScriptDirs) {
            triedLoadingPythonScriptDirs = true;
            PathW exePath = GetExePath().GetParentPath();
            exePath /= L"ScriptExeInfo.json";
            std::optional<JSON> json = DiskManager::LoadJson(exePath);
            if (json.has_value()) {
                JSON j = json.value();
                JSON wd = j["workDir"];
                workDir = StringW::FromNarrow(wd.get<std::string>().c_str());
                scriptPath = StringW::FromNarrow(j["scriptDir"].get<std::string>().c_str());
                args = j["args"].get<std::string>().c_str();
            }
        }
        if (ImGui::BeginMenu("Scripts")) {
            if (ImGui::BeginMenu("Python Script")) {
                ImGui::Text("Script:");
                ImGui::Text("%s",scriptPath.ToString().ToNarrow().CStr());
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FOLDER "##Script")) {
                    nfdu8char_t* outPath = nullptr;
                    const nfdu8filteritem_t filters[1] = { { "Python Script", "py" } };
                    if (NFD_OpenDialogU8(&outPath, filters, 1, nullptr) == NFD_OKAY) {
                        scriptPath = StringW::FromNarrow(outPath);
                        workDir = scriptPath.GetParentPath();
                        NFD_FreePathU8(outPath);
                        PathW exePath = GetExePath().GetParentPath();
                        exePath /= L"ScriptExeInfo.json";
                        JSON json = {
                            {"scriptDir", scriptPath.CStr()},
                            {"workDir", workDir.CStr()},
                            {"args", args.CStr()}
                        };
                        DiskManager::SaveJson(exePath.ToString(), json);
                    }
                }

                ImGui::Text("Work Dir:");
                ImGui::Text("%s",workDir.ToString().ToNarrow().CStr());
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FOLDER "##WorkDir")) {
                    nfdu8char_t* outPath = nullptr;
                    if (NFD_PickFolderU8(&outPath, nullptr) == NFD_OKAY) {
                        workDir = StringW::FromNarrow(outPath);
                        NFD_FreePathU8(outPath);
                        PathW exePath = GetExePath().GetParentPath();
                        exePath /= L"ScriptExeInfo.json";
                        JSON json = {
                            {"scriptDir", scriptPath.CStr()},
                            {"workDir", workDir.CStr()},
                            {"args", args.CStr()}
                        };
                        DiskManager::SaveJson(exePath.ToString(), json);
                    }
                }

                ImGui::Text("Args:");
                std::string tmp = args.CStr();
                if (ImGui::InputText("##", &tmp)) {
                    args = tmp.c_str();
                }
                if (ImGui::Button(ICON_FA_ROCKET "Run Script")) {
                    PathW exePath = GetExePath().GetParentPath();
                    exePath /= L"ScriptExeInfo.json";
                    JSON json = {
                        {"scriptDir", scriptPath.CStr()},
                        {"workDir", workDir.CStr()},
                        {"args", args.CStr()}
                    };
                    DiskManager::SaveJson(exePath.ToString(), json);
                    gEditorAppContext->EditorPythonManager->RunScript(scriptPath, workDir, args);
                }
                ImGui::EndMenu();
            }
            // Re-importing the modules re-registers every python class, which is what makes the scene
            // viewport recreate the live objects and components of those classes (SceneViewport's
            // "NewPythonType" subscription).
            if (ImGui::MenuItem("Reload Python Scripts")) {
                gEditorAppContext->EditorPythonManager->ClearProjectScripts();
                gEditorAppContext->EditorPythonManager->RunProjectScripts();
            }
            ImGui::EndMenu();
        }
        if (gEditorAppContext->EditorProjectManager->IsAnyProjectOpen()) {
            if (ImGui::BeginMenu("Build")) {
                if (ImGui::MenuItem("Build Project")) {
                    gEditorAppContext->EditorAssetManager->PrepareAssetsForDistribution(gEditorAppContext->EditorProjectManager->GetProjectCacheDirectory().ToString().ToNarrow());
                    gEditorAppContext->EditorShaderManager->PrepareShaderCodesForDistribution(gEditorAppContext->EditorProjectManager->GetProjectCacheDirectory().ToString().ToNarrow());
                    gEditorAppContext->EditorProjectManager->BuildProjectForShipment(gEditorAppContext->EditorProjectManager->GetProjectCacheDirectory());
                    PLU_INFO("Project is ready for shipment!");
                }
                ImGui::EndMenu();
            }
        }
        ImGui::PopStyleVar(); // menuBarSpacing
        ImGui::SameLine();
        ImVec2 const buttonDimensions = ImVec2(toolbarHeight,toolbarHeight);
        if (gEditorAppContext->EditorProjectManager->IsAnyProjectOpen() && gEditorAppContext->EditorScenesManager->IsAnySceneOpen() && windowID == 0) {
            ImGui::SetCursorPosX((sizeForPlayButton.x / 2) - (toolbarHeight / 2));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.3));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.8));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1,0,0,0));
            if (gEditorAppContext->EditorScenesManager->IsInPIE()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                if (ImGui::Button(ICON_FA_X "", buttonDimensions)) {
                    gEditorAppContext->EditorScenesManager->ExitPIE();
                    gPluEditor->EndGame();
                    gApplicationInfo->AppWindow->SetCursorVisibility(true);
                    gApplicationInfo->AppInputManager->GetInputBackend()->NotifyGameAboutInput = true;
                }
                ImGui::PopStyleColor();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.8f, 0.0f, 1.0f));
                if (!gApplicationInfo->AppInputManager->GetInputBackend()->NotifyGameAboutInput) {
                    if (ImGui::Button(ICON_FA_GAMEPAD "")) {
                        gApplicationInfo->AppInputManager->GetInputBackend()->NotifyGameAboutInput = true;
                        gApplicationInfo->AppWindow->SetCursorVisibility(false);
                        gApplicationInfo->AppWindow->UpdateImGui = false;
                    }
                    ImGui::PopStyleColor();
                    if (gApplicationInfo->AppScenesManager->GetEditorRenderCamera()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                        if (ImGui::Button(ICON_FA_BACKWARD ""))
                        {
                            gApplicationInfo->AppScenesManager->SetEditorRenderCamera(nullptr);
                        }
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.3f, 1.0f, 1.0f));
                        if (ImGui::Button(ICON_FA_CAMERA ""))
                        {
                            gApplicationInfo->AppScenesManager->SetEditorRenderCamera(gEditorAppContext->EditorSceneCamera.GetRaw());
                        }
                    }
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
                if (ImGui::Button(ICON_FA_PLAY "", buttonDimensions)) {
                    gPluEditor->StartGame();
                    if (gEditorAppContext->EditorScenesManager->EnterPIE()) {
                        gApplicationInfo->Client->JoinGameLocally();
                        gApplicationInfo->AppWindow->UpdateImGui = false;
                    } else {
                        gPluEditor->EndGame();
                    }
                }
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::Button(ICON_FA_CIRCLE_PLAY " Play In FullScreen")) {
                        gPluEditor->StartGame();
                        if (gEditorAppContext->EditorScenesManager->EnterPIE()) {
                            gApplicationInfo->Client->JoinGameLocally();
                            gEditorAppContext->PIEFullscreen = true;
                            gApplicationInfo->AppRenderingManager->SetImGuiRenderingIgnorance(true);
                            gApplicationInfo->AppWindow->SetCursorVisibility(false);
                        } else {
                            gPluEditor->EndGame();
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::Button("Close"))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();
        }
        const float textWidth = 400.0f * ImGui::GetStyle().FontScaleDpi;
        float availableWidth = ImGui::GetContentRegionAvail().x;
        float xCursor = ImGui::GetCursorPosX();
        // Right-aligned window controls: 3 square buttons (min / max / close) laid out in the menu
        // bar with ItemSpacing between them. Reserve their REAL width instead of the old magic
        // "buttonDimensions.x * 4" - that phantom 4th slot left a gap that grew/shrank inconsistently
        // vs font size (button size scales with font, ItemSpacing only with DPI), so the close button
        // drifted: too much gap at small fonts, overflowing the window at large ones. Keep one
        // ItemSpacing of breathing room on the right.
        const float ctrlSpacing = ImGui::GetStyle().ItemSpacing.x;
        const float controlsWidth = buttonDimensions.x * 3.0f + ctrlSpacing * 2.0f;
        const float controlsStartX = xCursor + availableWidth - controlsWidth - ctrlSpacing * 0.35f;
        ImGui::SetCursorPosX(controlsStartX - ImGui::GetFontSize() - textWidth);
        if (gEditorAppContext->EditorProjectManager->IsAnyProjectOpen()) {
            if (gEditorAppContext->EditorScenesManager->GetBaseSceneWorld()) {
                String msg = String::FromWide(gEditorAppContext->EditorProjectManager->GetProjectName().CStr());
                msg += " > ";
                msg += gEditorAppContext->EditorScenesManager->GetBaseSceneWorld()->Info->URL;
                ImGui::TextAligned(1, textWidth, msg.CStr());
            } else {
                ImGui::TextAligned(1, textWidth, String::FromWide(gEditorAppContext->EditorProjectManager->GetProjectName().CStr()).CStr());
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.6,0.6,1));
            ImGui::TextAligned(1, textWidth, "No Project Open!");
            ImGui::PopStyleColor();
        }
        ImGui::SetCursorPosX(controlsStartX);
        DrawWindowControls(gApplicationInfo->AppWindowsManager->GetWindow(static_cast<UInt32>(windowID)), buttonDimensions);
        ImGui::PopStyleVar();
        ImGui::EndMenuBar();
        ImGui::PopStyleVar(); // popup WindowPadding
        float h = ImGui::GetWindowHeight();
        ImGui::End();
        ImGui::PopStyleVar(3);
        return h;
    }

    inline void DrawMainEngineWindow(EditorWindowInfo& windowInfo)
    {
        const int windowID = static_cast<int>(windowInfo.WindowID);
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_NoDockingSplit;
        // Toolbar height must be purely proportional to the (already DPI-scaled) font size.
        // GetFontSize() = FontSizeBase * FontScaleMain * FontScaleDpi; using FontSizeBase would
        // collapse DrawToolbarWindow's FramePadding = (toolbarHeight - GetFontSize())/2 on HiDPI.
        // A fixed additive pad (the old "+12") made the bar disproportionately tall at small font
        // sizes and negligible at large ones - a constant ratio keeps it consistent everywhere.
        float toolbarHeight = ImGui::GetFontSize() * 1.6f;

        float realToolbarHeight = DrawToolbarWindow(toolbarHeight, windowID);
        realToolbarHeight = toolbarHeight;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + realToolbarHeight));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - realToolbarHeight));
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking |
                                        ImGuiWindowFlags_NoTitleBar |
                                        ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus |
                                        ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin(("PluEngine" + String::FromInt(windowID)).CStr(), nullptr, window_flags);
        ImGui::PopStyleVar(3);

        ImGuiIO& io = ImGui::GetIO();
        //Here we do dockspace for asset Viewports
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(10, 4));
            ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 8.0f);
            // Per window, not global: the ids belong to this window's record so panels and
            // viewports can dock into the window they were moved to, not into "the last one drawn".
            windowInfo.WindowClass.ClassId = ImGui::GetID(("EditorViewport" + String::FromInt(windowID)).CStr());
            windowInfo.DockspaceId = ImGui::GetID(("AssetDockspace" + String::FromInt(windowID)).CStr());
            ImGui::DockSpace(windowInfo.DockspaceId, ImVec2(0.0f, 0.0f), dockspace_flags, &windowInfo.WindowClass);
            ImGui::PopStyleVar(3);
        }
        ImGui::End();
    }

    inline void AssetSaveConfirm(bool* assetSaveConfirm, bool* closeWindow)
    {
        ImGui::OpenPopup("Assets are unsaved");
        if (ImGui::BeginPopupModal("Assets are unsaved")) {

            static DynamicArray<TUsePointer<AssetDescriptor>> assetsToSave;
            if (assetsToSave.IsEmpty()) {
                for (auto asset : gApplicationInfo->AppAssetManager->GetAllAssetDescriptorsOfType(IAssetData::GetStaticClass())) {
                    if (gApplicationInfo->AppAssetManager->IsAssetDirty(asset)) {
                        assetsToSave.PushBack(asset);
                    }
                }
            }

            auto cleanup = [closeWindow]() {
                assetsToSave.Clear();
                *closeWindow = true;
            };

            for (auto asset : assetsToSave) {
                ImGui::Text("%s", asset->AssetName.CStr());
            }

            ImGui::Separator();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6,0.6,1,1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4,0.4,1,1));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2,0.2,1,1));
            if (ImGui::Button("Save All")) {
                for (auto asset : assetsToSave) {
                    gApplicationInfo->AppAssetManager->SaveAsset(asset);
                }
                cleanup();
                *assetSaveConfirm = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            if (ImGui::Button("Don't Save")) {
                cleanup();
                *assetSaveConfirm = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                cleanup();
                *assetSaveConfirm = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}

#endif //PLUENGINE_EDITORINTERFACE_H