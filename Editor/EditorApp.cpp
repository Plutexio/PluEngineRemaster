//
// Created by Plutex on 12/29/25.
//

#include "EditorApp.h"

#include "EditorAppContext.h"
#include "EditorInterface.h"
#include "DefinedPanels/EngineStatsPanel.h"
#include "DefinedPanels/Style/EditorStylePanel.h"
#include "Managers/Project/EditorProjectManager.h"
#include "EditorSettings/EditorSettings.h"
#include "EditorSettings/EditorSettingsManager.h"
#include "PluEngine/Log.h"
#include "PluEngine/FrameDemand.h"
#include "PluEngine/Timer.h"
#include "PluEngine/Render/RenderingManager.h"
#include "PluEngine/Core/Objects/EngineObjectHandle.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Platform/Window.h"
#include "PluEngine/Platform/WindowsManager.h"
#include "Panels/EditorPanelManager.h"
#include "imgui_stdlib.h"
#include "Managers/Assets/EditorAssetImporter.h"
#include "PluEngine/AssetTypes/AnimationGraph/AnimationGraph.h"

#ifdef PLU_PLATFORM_WINDOWS
#include "imgui_impl_win32.h"
#elif defined(PLU_PLATFORM_LINUX)
#include "imgui_impl_sdl3.h"
#endif

#include "nfd.h"
#include "ImGuizmo.h"
#include "json_fwd.hpp"
#include "DefinedPanels/EngineClassTreePanel.h"
#include "EditorViewports/EditorViewportManager.h"
#include "EditorWindows/EditorWindowsManager.h"
#include "EditorViewports/IEditorPanel.h"
#include "Managers/Assets/EditorAssetManager.h"
#include "Managers/Python/EditorPythonManager.h"
#include "Managers/Scene/EditorCamera.h"
#include "Managers/Shaders/EditorShaderManager.h"
#include "PluEngine/Engine.h"
#include "PluEngine/PluPaths.h"
#include "PluEngine/Gameplay/GameClient.h"
#include "PluEngine/Gameplay/InputManager.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "UI/IconsFontAwesome7.h"
#include "Utils/CenteredText.h"

extern void InitEditorReflection();

//For editor only
Plu::TUsePointer<Plu::EngineObjectManager> gEngineObjectManager;
Plu::EditorAppContext* gEditorAppContext;
Plu::ApplicationInfo* gApplicationInfo;

Plu::PluEditor* gPluEditor;

Plu::PluEditor::PluEditor() : Application()
{
    gPluEditor = this;
}

Plu::PluEditor::~PluEditor()
{
}

bool Plu::PluEditor::OnInit()
{
    NFD_Init();
    InitEditorReflection();
    // Bridges TypeRegistry's generic "draw a reflected struct/class" hook (used by nested
    // PLU_STRUCT/PLU_CLASS fields, e.g. inside DynamicArray<T> elements) to its real
    // implementation. ReflectionBase.h can't call TypeSerializer<TypeInfo*>::EditorControl
    // directly (TypeTraits.h pulls in ImGui/pybind11/collision channels and already includes
    // ReflectionBase.h back), so it's wired once here instead — without this, any reflected
    // struct/class field renders as "Unsupported type X!".
    // (The serialize/deserialize counterparts are shared with the runtime: Application::EngineInit.)
    Plu::TypeRegistry::GetInstance()->editorControlForTypeInfo = &Plu::TypeSerializer<Plu::TypeInfo*>::EditorControl;
    mEditorAppContext = new EditorAppContext;
    Plu::WindowProperties props;
    props.Title = "Plu Editor";
    props.Borderless = true;
    mApplicationInfo.AppWindow = IWindow::PlutexCreateWindow(props, mObjectManager, &mApplicationInfo);
    TUsePointer<EditorProjectManager> projectManager = mObjectManager->CreateObject(EditorProjectManager::GetStaticClass());
    mEditorProjectManager = mObjectManager->GetObjectAsOwner<EditorProjectManager>(projectManager->GetObjectHandle());
    mEditorProjectManager->SetEditorAppContext(mEditorAppContext, &mApplicationInfo);
    mEditorAppContext->EditorPythonManager = mObjectManager->CreateObject(EditorPythonManager::GetStaticClass());
    mEditorAppContext->EditorViewportManager = mObjectManager->CreateObject(EditorViewportManager::GetStaticClass());
    mEditorAppContext->EditorShaderManager = mObjectManager->CreateObject(EditorShaderManager::GetStaticClass());
    const EngineObjectHandle panelManagerHandle = mObjectManager->CreateObject<EditorPanelManager>();
    mPanelManager = mObjectManager->GetObjectAsOwner<EditorPanelManager>(panelManagerHandle);
    PLU_INFO("Editor Init");
    gEditorAppContext = mEditorAppContext;
    gEngineObjectManager = mObjectManager;
    gApplicationInfo = &mApplicationInfo;
    mEditorAppContext->EditorAssetManager = gApplicationInfo->AppAssetManager;
    mEditorAppContext->EditorScenesManager = gApplicationInfo->AppScenesManager;
    mEditorAppContext->EditorShaderManager->PreInit(mEditorProjectManager);
    mEditorAppContext->EditorPanelManager = mPanelManager;
    mEditorAppContext->EditorProjectManager =  mEditorProjectManager;
    mPanelManager->Init(&mApplicationInfo, mEditorAppContext);
    mPanelManager->Init();
    mApplicationInfo.AppScenesManager = mEditorAppContext->EditorScenesManager;
    mApplicationInfo.AppShaderManager = mEditorAppContext->EditorShaderManager;
    mEditorAppContext->EditorWindowsManager = mObjectManager->CreateObject(EditorWindowsManager::GetStaticClass());
    mEditorAppContext->EditorWindowsManager->Initialize(&mApplicationInfo, mEditorAppContext);
    mEditorAppContext->EditorWindowsManager->RegisterMainWindow(props.Title);

    EngineObjectHandle inputManagerHandle = mObjectManager->CreateObject<InputManager>();
    mApplicationInfo.AppInputManager = mObjectManager->GetObjectAsUser<InputManager>(inputManagerHandle);
    return true;
}

void Plu::PluEditor::OnPostInit()
{
    // Zapamiętane ustawienia Display (VSync / tryb okna / rozdzielczość). Musi być po
    // AppWindow->Init() i jeszcze na main threadzie — przed oddaniem kontekstu GL.
    EditorSettingsManager::GetInstance()->ApplyDisplaySettings(mApplicationInfo.AppWindow);
    SetPowerSavingEnabled(EditorSettingsManager::GetInstance()->GetSettings()->PowerSaving);

    mEditorAppContext->EditorSceneCamera = mObjectManager->CreateObject(EditorSceneCamera::GetStaticClass());
    mApplicationInfo.AppScenesManager->GetObjectEventDispatcher()->Subscribe("EditorCameraWanted", [this](void* data) {
        IRendererCamera** cameraFieldPtr = static_cast<IRendererCamera**>(data);
        *cameraFieldPtr = mEditorAppContext->EditorSceneCamera.GetRaw();
    });

    mApplicationInfo.AppScenesManager->GetObjectEventDispatcher()->Subscribe("EditorCameraLocationToSave", [this](void* data) {
        Vec3* location = static_cast<Vec3*>(data);
        *location = mEditorAppContext->EditorSceneCamera->GetCameraLocation();
    });
    mApplicationInfo.AppScenesManager->GetObjectEventDispatcher()->Subscribe("EditorCameraRotationToSave", [this](void* data) {
        Vec3* rotation = static_cast<Vec3*>(data);
        *rotation = mEditorAppContext->EditorSceneCamera->GetHumanReadableRotation();
    });

    mApplicationInfo.AppScenesManager->GetObjectEventDispatcher()->Subscribe("EditorCameraLocationLoaded", [this](void* data) {
        Vec3* location = static_cast<Vec3*>(data);
        mEditorAppContext->EditorSceneCamera->SetCameraLocation(*location);
    });
    mApplicationInfo.AppScenesManager->GetObjectEventDispatcher()->Subscribe("EditorCameraRotationLoaded", [this](void* data) {
        Vec3* rotation = static_cast<Vec3*>(data);
        mEditorAppContext->EditorSceneCamera->SetCameraRotation(*rotation);
    });

    mApplicationInfo.AppWindow->GetObjectEventDispatcher()->Subscribe("FileDragEntered", [this](void* data) {
        mIsDropOnWindow = true;
        mPathsToImport.Clear();
    });
    mApplicationInfo.AppWindow->GetObjectEventDispatcher()->Subscribe("FileDragEnded", [this](void* data) {
        mIsDropOnWindow = false;
    });
    mApplicationInfo.AppWindow->GetObjectEventDispatcher()->Subscribe("FileDropped", [this](void* data) {
        if (!mEditorProjectManager->IsAnyProjectOpen()) return;
        Path* path = static_cast<Path*>(data);
        if (!path) return;
        mPathsToImport.PushBack(*path);
    });


    ImGui::SetCurrentContext(mApplicationInfo.AppWindow->GetImGuiContext());
    //Fonts
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    //io.Fonts->AddFontDefault(); // Ładujemy standardową czcionkę

    static constexpr ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    ImFontConfig icons_config;
    icons_config.MergeMode = true; // KLUCZOWE: to łączy ikony z tekstem
    icons_config.PixelSnapH = true;

    // Ścieżka do pliku .ttf
    // String path = "";
    // path += PLU_FONTS_DIR;
    // path += "Font Awesome 7 Free-Regular-400.otf";

    std::string pathStd = GetEngineResourcesDir().Append(L"ThirdParty/UI/Fonts/").ToString().ToNarrow().CStr();
    pathStd += "Font Awesome 7 Free-Regular-400.otf";
    std::string path2 = GetEngineResourcesDir().Append(L"ThirdParty/UI/Fonts/").ToString().ToNarrow().CStr();
    path2 += "Font Awesome 7 Free-Solid-900.otf";
    std::string pathOpenSans = GetEngineResourcesDir().Append(L"ThirdParty/UI/Fonts/").ToString().ToNarrow().CStr();
    pathOpenSans += "OpenSans-Regular.ttf";

    io.Fonts->AddFontFromFileTTF(pathOpenSans.c_str(), 19.0f);
    io.Fonts->AddFontFromFileTTF(pathStd.c_str(), 13.0f, &icons_config, icons_ranges);
    io.Fonts->AddFontFromFileTTF(path2.c_str(), 13.0f, &icons_config, icons_ranges);
}

void Plu::PluEditor::OnShutdown()
{
    PLU_INFO("Editor Shutdown");
    mEditorAppContext->EditorScenesManager->ExitPIE();
    EndGame();
    // Backstop only. The good save happens in OnRequestedWindowClose, before the closes that come
    // with quitting take the secondary windows apart; saving again here would overwrite it with a
    // layout that has already lost them. This branch covers a shutdown that never went through a
    // close request at all.
    if (!mLayoutSavedOnQuit) mEditorAppContext->EditorWindowsManager->SaveLayout();
    mEditorAppContext->EditorWindowsManager->Shutdown();
    mPanelManager->Shutdown();
    mEditorAppContext->EditorViewportManager->Shutdown();
    mEditorAppContext->EditorScenesManager->SetEditorRenderCamera(nullptr);
    mObjectManager->DestroyObject(*mEditorAppContext->EditorSceneCamera->GetEngineObjectHandle());
    mEditorAppContext->EditorProjectManager->Shutdown();
    mObjectManager->DestroyObject(*mEditorAppContext->EditorViewportManager->GetEngineObjectHandle());
    mObjectManager->DestroyObject(*mEditorAppContext->EditorScenesManager->GetEngineObjectHandle());
    mObjectManager->DestroyObject(*mEditorAppContext->EditorAssetManager->GetEngineObjectHandle());
    mObjectManager->DestroyObject(*mEditorAppContext->EditorPanelManager->GetEngineObjectHandle());
    mObjectManager->DestroyObject(*mEditorAppContext->EditorProjectManager->GetEngineObjectHandle());
    delete mEditorAppContext;
    NFD_Quit();
}

float lastDeltaTime = 0.0f;

void Plu::PluEditor::OnImGuiRender()
{
    ImGui::SetCurrentContext(mApplicationInfo.AppWindow->GetImGuiContext());
    if (mEditorAppContext->PIEFullscreen) {
        if (mApplicationInfo.AppInputManager->GetInputBackend()->GetKeyboard().IsDown(Key::Escape)) {
            mEditorAppContext->EditorScenesManager->ExitPIE();
            EndGame();
            mEditorAppContext->PIEFullscreen = false;
            gApplicationInfo->AppRenderingManager->SetImGuiRenderingIgnorance(false);
            gApplicationInfo->AppWindow->SetCursorVisibility(true);
        }
        return;
    }
    if (gEditorAppContext->EditorScenesManager->IsInPIE()) {
        if (mApplicationInfo.AppInputManager->GetInputBackend()->GetKeyboard().IsDown(Key::F8)) {
            mApplicationInfo.AppInputManager->GetInputBackend()->NotifyGameAboutInput = false;
            gApplicationInfo->AppWindow->SetCursorVisibility(true);
            gApplicationInfo->AppWindow->UpdateImGui = true;
        }
        if (mApplicationInfo.AppInputManager->GetInputBackend()->GetKeyboard().IsDown(Key::Escape)) {
            mEditorAppContext->EditorScenesManager->ExitPIE();
            EndGame();
            gApplicationInfo->AppWindow->SetCursorVisibility(true);
            gApplicationInfo->AppWindow->UpdateImGui = true;
            gApplicationInfo->AppInputManager->GetInputBackend()->NotifyGameAboutInput = true;
        }
    }
    if (EditorWindowInfo* mainWindowInfo = mEditorAppContext->EditorWindowsManager->GetWindowInfo(0)) {
        DrawMainEngineWindow(*mainWindowInfo);
    }
    if (mEditorAppContext->NewProjectPopup)
    {
        ImGui::OpenPopup("Create New Project");
        mEditorAppContext->NewProjectPopup = false;
    }
    DrawNewProjectPopup();

    bool dockedViewports = false;
    bool dockedPanels = false;

    if (mPanelManager->AreTherePanelsToDock()) {
        mPanelManager->InitNewPanels();
        dockedPanels = true;
    }

    if (mEditorAppContext->EditorViewportManager->AreThereViewportsToDock() && !dockedPanels) {
        mEditorAppContext->EditorViewportManager->DockNewViewports(0);
        dockedViewports = true;
    }

    mEditorAppContext->EditorViewportManager->Tick(lastDeltaTime, 0);
    mPanelManager->OnUpdate(lastDeltaTime, 0);
    // The importer dispatches "Finito" from inside RenderUI(), so it may only be destroyed after
    // that call returns — ClearAfterImport() just raises the flag.
    if (mAssetImporter) {
        if (!mAssetImportFinished) mAssetImporter->RenderUI();
        if (mAssetImportFinished) {
            EngineObjectHandle importerHandle = mAssetImporter->GetObjectHandle();
            mAssetImporter = nullptr;
            mApplicationInfo.AppObjectManager->DestroyObject(importerHandle);
            mAssetImportFinished = false;
        }
    }

    if (dockedPanels) {
        mPanelManager->DockNewPanels(0);
    }

    if (mIsDropOnWindow) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoDocking;
        ImGui::Begin("FileDropInfo", nullptr, flags);
        ImGui::GetCurrentWindow()->DrawList->AddRectFilled(
            viewport->WorkPos,
            viewport->Size,
            mEditorProjectManager->IsAnyProjectOpen() ? IM_COL32(0, 0, 150, 80) : IM_COL32(90, 0, 0, 80)
        );
        if (mEditorProjectManager->IsAnyProjectOpen()) {
            TextCenteredBoth("Drop Assets to import them!");
        } else {
            TextCenteredBoth("Before importing Assets, Open a project!");
        }
        ImGui::End();
    }
}

// Windows-reserved filename characters. Good enough cross-platform since we never want these
// in a project name regardless of host OS (keeps project folders portable).
static bool HasInvalidProjectNameChars(const Plu::String& name)
{
    static const char* invalidChars = "\\/:*?\"<>|";
    for (const char* c = invalidChars; *c; ++c) {
        if (name.Find(*c) != Plu::String::Npos) return true;
    }
    return false;
}

void Plu::PluEditor::DrawNewProjectPopup()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);

    // Fully opaque popup - overrides the engine's normal glassy/translucent PopupBg just for this
    // window, since a see-through modal reads poorly with code/desktop showing through behind it.
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
    bool isOpen = ImGui::BeginPopupModal("Create New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);
    ImGui::PopStyleColor();
    if (!isOpen) {
        return;
    }

    static String pathToNewProject;
    static String projectName;

    const ImVec4 accent      = ImVec4(0.36f, 0.47f, 0.98f, 1.00f);
    const ImVec4 accentGlow  = ImVec4(0.55f, 0.40f, 0.98f, 1.00f);
    const ImVec4 warnColor   = ImVec4(1.00f, 0.65f, 0.20f, 1.00f);
    const ImVec4 okColor     = ImVec4(0.35f, 0.85f, 0.50f, 1.00f);

    bool justAppeared = ImGui::IsWindowAppearing();
    if (justAppeared) {
        projectName.Clear();
        projectName.Reserve(30);
    }

    // All sizes below are derived from the current (DPI-scaled) font size rather than hardcoded
    // pixels - this popup's layout must track the same mainScale/FontScaleDpi factor the rest of
    // the engine style is scaled by (see ImGuiRenderState::CreateContext), otherwise it mismatches on
    // any monitor that isn't 100% scale and text overlaps or clips.
    float fs = ImGui::GetFontSize();
    float uiScale = fs / 13.0f;

    // Fixed logical content width. AlwaysAutoResize recomputes the window size from this frame's
    // content, so item widths must NOT be derived from GetWindowSize() - that reads back last
    // frame's (already-grown) size and creates a runaway feedback loop that grows the popup every
    // frame. Header art below is still drawn to span the actual window rect since draw-list calls
    // don't feed into autosize.
    const float contentWidth = 360.0f * uiScale;
    const float sideMargin = 20.0f * uiScale;

    // --- Header: gradient banner with a big icon tile, title and subtitle -------------------
    ImVec2 winPos = ImGui::GetWindowPos();
    float winWidth = ImGui::GetWindowSize().x;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    float titleFontSize = fs * 1.3f;
    ImVec2 titleTextSize = ImGui::GetFont()->CalcTextSizeA(titleFontSize, FLT_MAX, 0.0f, "Create New Project");
    ImVec2 subtitleTextSize = ImGui::CalcTextSize("Set up a fresh PluEngine workspace");
    float textBlockHeight = titleTextSize.y + ImGui::GetStyle().ItemSpacing.y + subtitleTextSize.y;

    ImVec2 iconTileSize = ImVec2(46.0f * uiScale, 46.0f * uiScale);
    float headerVPad = ImGui::GetStyle().WindowPadding.y;
    float headerHeight = (iconTileSize.y > textBlockHeight ? iconTileSize.y : textBlockHeight) + headerVPad * 2.0f;

    ImVec2 headerMin = winPos;
    ImVec2 headerMax = ImVec2(winPos.x + winWidth, winPos.y + headerHeight);
    // The window background is rounded (PopupRounding) at its top corners. AddRectFilledMultiColor
    // has no rounding parameter, so a gradient fill here would paint sharp corners back into the
    // area the rounded window bg deliberately left transparent. Use a single flat rounded-top fill
    // instead - keeps the corners clean with no seam between layered draws.
    float cornerRounding = ImGui::GetStyle().PopupRounding;
    drawList->AddRectFilled(headerMin, headerMax,
                             ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.38f)),
                             cornerRounding, ImDrawFlags_RoundCornersTop);
    // Animated on a clock, not on input: under power saving it has to ask for its frames. A slow
    // glow (one cycle in ~3 s) reads fine at 20 fps, and this screen sits open with nobody touching it.
    RequestRedrawAfter(0.05f);
    float pulse = 0.5f + 0.5f * sinf(static_cast<float>(ImGui::GetTime()) * 2.0f);
    ImVec4 glowLine = ImVec4(accent.x + (accentGlow.x - accent.x) * pulse,
                              accent.y + (accentGlow.y - accent.y) * pulse,
                              accent.z + (accentGlow.z - accent.z) * pulse, 0.9f);
    drawList->AddLine(ImVec2(headerMin.x, headerMax.y), ImVec2(headerMax.x, headerMax.y),
                       ImGui::ColorConvertFloat4ToU32(glowLine), 2.0f);

    ImGui::Dummy(ImVec2(0, headerVPad * 0.5f));
    ImGui::Indent(sideMargin);

    ImVec2 iconTileMin = ImVec2(ImGui::GetCursorScreenPos().x, winPos.y + (headerHeight - iconTileSize.y) * 0.5f);
    ImVec2 iconTileMax = ImVec2(iconTileMin.x + iconTileSize.x, iconTileMin.y + iconTileSize.y);
    drawList->AddRectFilled(iconTileMin, iconTileMax, ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.55f)), 10.0f * uiScale);
    float iconFontSize = fs * 1.6f;
    ImVec2 iconTextSize = ImGui::GetFont()->CalcTextSizeA(iconFontSize, FLT_MAX, 0.0f, ICON_FA_FOLDER_PLUS);
    drawList->AddText(ImGui::GetFont(), iconFontSize,
                       ImVec2(iconTileMin.x + (iconTileSize.x - iconTextSize.x) * 0.5f, iconTileMin.y + (iconTileSize.y - iconTextSize.y) * 0.5f),
                       IM_COL32(255, 255, 255, 255), ICON_FA_FOLDER_PLUS);

    ImGui::SetCursorScreenPos(ImVec2(iconTileMax.x + 14.0f * uiScale, winPos.y + (headerHeight - textBlockHeight) * 0.5f));
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, titleFontSize);
    ImGui::TextUnformatted("Create New Project");
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.55f));
    ImGui::TextUnformatted("Set up a fresh PluEngine workspace");
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    ImVec2 closeButtonSize = ImVec2(fs * 1.85f, fs * 1.85f);
    float closeButtonMargin = headerVPad > sideMargin * 0.6f ? headerVPad : sideMargin * 0.6f;
    ImGui::SetCursorScreenPos(ImVec2(headerMax.x - closeButtonSize.x - closeButtonMargin, winPos.y + (headerHeight - closeButtonSize.y) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.25f));
    if (ImGui::Button(ICON_FA_XMARK "##CloseNewProjectPopup", closeButtonSize)) {
        projectName.Clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(3);

    ImGui::Unindent(sideMargin);
    ImGui::SetCursorScreenPos(ImVec2(winPos.x, headerMax.y + 2));
    ImGui::Dummy(ImVec2(0, headerVPad));

    // --- Body ---------------------------------------------------------------------------------
    ImGui::Indent(sideMargin);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.55f));
    ImGui::TextUnformatted("PROJECT NAME");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(contentWidth);
    if (justAppeared) ImGui::SetKeyboardFocusHere();
    std::string nameBuffer = projectName.CStr();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.08f, 0.6f));
    if (ImGui::InputTextWithHint("##ProjectNamePrompt", ICON_FA_CUBE "  MyAwesomeGame", &nameBuffer)) {
        projectName = nameBuffer.c_str();
    }
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 10.0f * uiScale));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.55f));
    ImGui::TextUnformatted("LOCATION");
    ImGui::PopStyleColor();

    float browseButtonWidth = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x * 2 + 6;
    ImGui::SetNextItemWidth(contentWidth - browseButtonWidth - ImGui::GetStyle().ItemSpacing.x);
    std::string pathBuffer = pathToNewProject.CStr();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.08f, 0.6f));
    if (ImGui::InputTextWithHint("##ProjectLocationPrompt", ICON_FA_FOLDER "  Choose a folder...", &pathBuffer)) {
        pathToNewProject = pathBuffer.c_str();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accentGlow);
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "##SelectProjectDir", ImVec2(browseButtonWidth, 0))) {
        nfdu8char_t* outPath = nullptr;
        if (NFD_PickFolderU8(&outPath, nullptr) == NFD_OKAY) {
            pathToNewProject = outPath;
            NFD_FreePathU8(outPath);
        }
    }
    ImGui::PopStyleColor(2);

    // --- Live validation & path preview --------------------------------------------------------
    bool nameEmpty = projectName.IsEmpty();
    bool nameInvalid = !nameEmpty && HasInvalidProjectNameChars(projectName);
    bool locationEmpty = pathToNewProject.IsEmpty();
    bool locationMissing = !locationEmpty && !std::filesystem::exists(pathToNewProject.CStr());

    Path previewPath = pathToNewProject + "/" + projectName;
    previewPath = previewPath.GetNormalized();
    bool alreadyExists = !nameEmpty && !locationEmpty && !locationMissing && std::filesystem::exists(previewPath.CStr());

    bool valid = !nameEmpty && !nameInvalid && !locationEmpty && !locationMissing && !alreadyExists;

    ImGui::Dummy(ImVec2(0, 12.0f * uiScale));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 8.0f * uiScale));

    if (valid) {
        ImGui::PushStyleColor(ImGuiCol_Text, okColor);
        ImGui::TextWrapped(ICON_FA_CIRCLE_CHECK "  %s", previewPath.CStr());
        ImGui::PopStyleColor();
    } else {
        const char* message = "Enter a project name to get started.";
        if (nameInvalid) message = "Project name contains invalid characters ( \\ / : * ? \" < > | ).";
        else if (locationEmpty && !nameEmpty) message = "Choose a location for the project.";
        else if (locationMissing) message = "Chosen location does not exist.";
        else if (alreadyExists) message = "A project already exists at this location.";
        ImGui::PushStyleColor(ImGuiCol_Text, warnColor);
        ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s", message);
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, 14.0f * uiScale));

    // --- Actions --------------------------------------------------------------------------------
    ImVec2 createSize = ImVec2(130.0f * uiScale, 0);
    ImVec2 cancelSize = ImVec2(90.0f * uiScale, 0);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + contentWidth - createSize.x - cancelSize.x - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Cancel", cancelSize)) {
        projectName.Clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!valid);
    ImGui::PushStyleColor(ImGuiCol_Button, accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accentGlow);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accentGlow);
    if (ImGui::Button(ICON_FA_ROCKET "  Create", createSize)) {
        mEditorProjectManager->CreateNewProject(StringW::FromNarrow(pathToNewProject.CStr()), projectName);
        pathToNewProject = "";
        projectName = "";
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    ImGui::Unindent(sideMargin);
    ImGui::EndPopup();
}

void Plu::PluEditor::DrawSinglePanelWindow(EditorWindowInfo& windowInfo)
{
    // No toolbar and no dockspace: a title bar carrying the window controls, and below it the one
    // panel this window exists for, filling everything that is left.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float titleBarHeight = ImGui::GetFontSize() * 1.6f;
    TUsePointer<IWindow> window = mApplicationInfo.AppWindowsManager->GetWindow(windowInfo.WindowID);

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, titleBarHeight));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags barFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                ImGuiWindowFlags_MenuBar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(("PluPanelWindowBar" + String::FromInt(static_cast<int>(windowInfo.WindowID))).CStr(), nullptr, barFlags);
    if (ImGui::BeginMenuBar()) {
        // Square buttons sized off the font like the toolbar's, not off the bar height — a button
        // as tall as the whole bar overflows it once the menu bar's own padding is added.
        const ImVec2 buttonDimensions(ImGui::GetFontSize() * 1.6f, ImGui::GetFontSize() * 1.6f);
        ImGui::TextUnformatted(windowInfo.Title.CStr());
        // Right-aligned: reserve the real width of the cluster, ItemSpacing between the buttons
        // included, otherwise the close button hangs past the right edge (same trap as the toolbar).
        const float ctrlSpacing = ImGui::GetStyle().ItemSpacing.x;
        const float controlsWidth = buttonDimensions.x * 3.0f + ctrlSpacing * 2.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - controlsWidth - ctrlSpacing * 0.35f);
        DrawWindowControls(window, buttonDimensions);
        ImGui::EndMenuBar();
    }
    const float realBarHeight = ImGui::GetWindowHeight();
    ImGui::End();
    ImGui::PopStyleVar(3);

    if (!windowInfo.HostedPanel) return;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + realBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - realBarHeight));
    ImGui::SetNextWindowViewport(viewport->ID);
    windowInfo.HostedPanel->OnUpdate(lastDeltaTime);
}

void Plu::PluEditor::OnImGuiRenderForWindow(EditorWindowInfo& windowInfo)
{
    // The caller (OnTick) has already made this window's ImGui context current; every ImGui call
    // below — including GetMainViewport() inside DrawMainEngineWindow — resolves against it.
    switch (windowInfo.Kind) {
        case EEditorWindowKind::Main:
        {
            OnImGuiRender();
            if (mAssetSaveConfirmShow) {
                bool closeWindow = false;
                AssetSaveConfirm(&mAssetSaveConfirm, &closeWindow);
                if (mAssetSaveConfirm && closeWindow) {
                    DispatchWindowClose(gApplicationInfo->AppWindow);
                }
                if (closeWindow) {
                    mAssetSaveConfirmShow = false;
                }
            }
            break;
        }
        case EEditorWindowKind::Dockspace:
        {
            // Toolbar + this window's own root dockspace. Popups, the drop overlay and the asset
            // importer stay with window 0 — they belong to the application, not to a dockspace.
            DrawMainEngineWindow(windowInfo);
            mEditorAppContext->EditorViewportManager->DockNewViewports(windowInfo.WindowID);
            mPanelManager->DockNewPanels(windowInfo.WindowID);
            mEditorAppContext->EditorViewportManager->Tick(lastDeltaTime, windowInfo.WindowID);
            mPanelManager->OnUpdate(lastDeltaTime, windowInfo.WindowID);
            break;
        }
        case EEditorWindowKind::SinglePanel:
        {
            DrawSinglePanelWindow(windowInfo);
            break;
        }
    }
}

// True if any ImGui texture still has work in flight that the render thread will act on - either a
// pending create/update/destroy (Status != OK) or a destroy QUEUED for next frame
// (WantDestroyNextFrame, Status still OK this frame). A frame that leaves work pending MUST be
// handed to the render thread in lockstep: its draw commands reference those textures and the
// render thread has to upload them BEFORE it draws (otherwise GetTexID()==0 asserts), and the atlas
// must not be mutated again until that pass is done.
//
// WantDestroyNextFrame is the crucial one: when a font-atlas rebuild creates a new texture it marks
// the old one WantDestroyNextFrame but leaves its Status at OK for this frame. Checking Status alone
// would let us drop out of lockstep here, and then NEXT frame the Main thread flips that texture to
// WantDestroy / bumps UnusedFrames while the render thread is concurrently rendering the previous
// snapshot (which still lists the old texture) and destroys it mid-flight -> GetTexID()==0. Staying
// in lockstep until the whole create->destroy->remove flush is done (a few frames, UnusedFrames-
// gated) keeps that destroy frame on the Main thread alone. Call after ImGui::Render() (when
// GetPlatformIO().Textures is up to date).
static bool ImGuiHasPendingTextureWork()
{
    const ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    for (ImTextureData* tex : platformIo.Textures) {
        if (tex->Status != ImTextureStatus_OK || tex->WantDestroyNextFrame) {
            return true;
        }
    }
    return false;
}

// Power saving: how often the shader/script watchers are polled, in frames or out of them.
static constexpr float kFileWatchIntervalSeconds = 1.5f;
// Half of ImGui's text cursor blink period (InputTextCursorBlink: 1.2 s cycle, visible ~0.8 s) is
// not exposed, so redraw often enough to catch both edges.
static constexpr float kTextCursorBlinkSeconds = 0.4f;

// Order-dependent 64-bit hash over raw bytes, eight at a time. Only has to tell "same draw data
// as last frame" from "different" — not cryptographic, not stable across runs.
static UInt64 HashBytes(UInt64 hash, const void* data, size_t size)
{
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    while (size >= sizeof(UInt64)) {
        UInt64 word;
        memcpy(&word, bytes, sizeof(word));
        hash = (hash ^ word) * 0x9E3779B97F4A7C15ULL;
        hash ^= hash >> 32;
        bytes += sizeof(word);
        size -= sizeof(word);
    }
    UInt64 tail = size;
    memcpy(&tail, bytes, size);
    hash = (hash ^ tail) * 0x9E3779B97F4A7C15ULL;
    return hash ^ (hash >> 32);
}

// Everything that decides what a window's ImGui frame looks like on screen: geometry, clip rects,
// textures, draw order. Two frames with the same hash present the same pixels (the scene image is
// a texture id here — its contents are the scene snapshot's business, not this hash's).
static UInt64 HashImGuiDrawData(UInt64 hash, UInt32 windowID, const ImDrawData* drawData)
{
    hash = HashBytes(hash, &windowID, sizeof(windowID));
    if (!drawData || !drawData->Valid) return hash;
    hash = HashBytes(hash, &drawData->DisplayPos, sizeof(drawData->DisplayPos));
    hash = HashBytes(hash, &drawData->DisplaySize, sizeof(drawData->DisplaySize));
    hash = HashBytes(hash, &drawData->FramebufferScale, sizeof(drawData->FramebufferScale));
    for (const ImDrawList* list : drawData->CmdLists) {
        hash = HashBytes(hash, list->VtxBuffer.Data, static_cast<size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert));
        hash = HashBytes(hash, list->IdxBuffer.Data, static_cast<size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
        // Field by field: ImDrawCmd has padding and a callback pointer.
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            // The raw reference, not GetTexID(): that asserts on a texture the render thread has
            // not uploaded yet (a fresh font atlas), which is a normal state on this thread.
            hash = HashBytes(hash, &cmd.ClipRect, sizeof(cmd.ClipRect));
            hash = HashBytes(hash, &cmd.TexRef._TexData, sizeof(cmd.TexRef._TexData));
            hash = HashBytes(hash, &cmd.TexRef._TexID, sizeof(cmd.TexRef._TexID));
            hash = HashBytes(hash, &cmd.VtxOffset, sizeof(cmd.VtxOffset));
            hash = HashBytes(hash, &cmd.IdxOffset, sizeof(cmd.IdxOffset));
            hash = HashBytes(hash, &cmd.ElemCount, sizeof(cmd.ElemCount));
        }
    }
    return hash;
}

static bool IsAnyImGuiKeyDown()
{
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++) {
        if (ImGui::IsKeyDown(static_cast<ImGuiKey>(key))) return true;
    }
    return false;
}

void Plu::PluEditor::ClearAfterImport()
{
    // Called from the importer's "Finito" event, i.e. from inside its own RenderUI() — destroying
    // it here would free the object (and this very callback) mid-dispatch. OnImGuiRender does it.
    mAssetImportFinished = true;
    mPathsToImport.Clear();
}

void Plu::PluEditor::OnTick(float deltaTime)
{
    lastDeltaTime = deltaTime;
    if (mEditorAppContext->EditorScenesManager->GetCurrentWorld() && !mEditorAppContext->EditorScenesManager->IsInPIE()) {
        mEditorAppContext->EditorScenesManager->GetCurrentWorld()->HandleDestroy();
    }
    mEditorAppContext->EditorWindowsManager->OnUpdate(deltaTime);
    PollFileWatchers(deltaTime);
    static int frameCounter = 0;
    if (frameCounter < 5) frameCounter++;
    if (frameCounter >= 5 && !mEditorProjectManager->IsAnyProjectOpen() && mArgumentParser) {
        // Once only, a few frames in. A --project that cannot be opened used to re-attempt (and
        // re-log the failure) for the lifetime of the process, burying the actual error under
        // hundreds of thousands of lines.
        static bool startupProjectTried = false;
        if (!startupProjectTried) {
            startupProjectTried = true;
            try {
                std::string projectPath = mArgumentParser->get<std::string>("project");
                mEditorProjectManager->OpenProject(StringW::FromNarrow(projectPath.c_str()));
            } catch (...) {

            }
        }
    }

    if (!mPathsToImport.IsEmpty()) {
        if (!mEditorProjectManager->IsAnyProjectOpen()) {
            mPathsToImport.Clear();
        } else {
            if (!mAssetImporter) {
                mAssetImporter = mApplicationInfo.AppObjectManager->CreateObject(EditorAssetImporter::GetStaticClass());
                mAssetImporter->GetObjectEventDispatcher()->Subscribe("Finito", [this](void*) {
                    ClearAfterImport();
                });
                mAssetImporter->Initialize(mPathsToImport, &mApplicationInfo);
            }
        }
    }

    // Build this frame's ImGui UI on the Main thread, then hand the draw data to the render
    // thread. ImGui_ImplSDL2_NewFrame()/ImGui::NewFrame() run here (input + display size, no
    // GL); the OpenGL3 backend submits the cloned draw data on the render thread.
    if (ImGuiContext* ctx = mApplicationInfo.AppWindow->GetImGuiContext()) {
        PLU_PROFILE_SCOPE("ImGui Build");
        ImGui::SetCurrentContext(ctx);

        // ImGui 1.92 dynamic fonts rebuild/grow the shared font atlas (create a new ImTextureData,
        // destroy the old) whenever glyphs are (re)baked - not only on the frame the font size
        // changes, but on later frames too as new glyphs are lazily baked at the new size. The
        // lock-free ImGui handoff only deep-copies draw lists, not texture state, so if the atlas
        // mutates while the render thread is reading a snapshot the two threads race: the render
        // thread either asserts in ImGui's create/destroy paths, or draws a freshly created texture
        // before it was uploaded (GetTexID()==0). While any texture work is in flight we drive the
        // handoff in lockstep so only one thread touches the atlas at a time.
        //
        // Engage BEFORE NewFrame() when we already know a mutation is coming (queued font-size
        // change) or a previous frame left work in flight - this covers the destroy side, where the
        // mutation would otherwise race a render-thread read of the prior snapshot.
        const ImGuiStyle& style = ImGui::GetStyle();
        bool lockstepEngaged = (style._NextFrameFontSizeBase != 0.0f) || mImGuiAtlasSettling;
        if (lockstepEngaged) {
            mApplicationInfo.AppRenderingManager->BeginImGuiLockstep();
        }

        // One frame per window, all published together (EndImGuiFrameSubmit) so the render thread
        // never mixes windows from different frames. Each window has its own ImGui context; they
        // share the font atlas, which is why the lockstep above wraps the whole loop rather than
        // any single window.
        // Snapshot: building a window's UI may add a window (the "Move to New Window" menu), and
        // that would reallocate the manager's array mid-iteration. Records themselves are heap
        // allocated and only freed at the start of a frame, so the pointers stay valid.
        DynamicArray<EditorWindowInfo*> windowsThisFrame = mEditorAppContext->EditorWindowsManager->GetWindows();
        UInt32 builtWindowCount = 0;
        // Windows built this frame, submitted together after the loop. A context's draw data stays
        // valid until its own next NewFrame(), so deferring the copy is safe — and lets a probe
        // frame that changed nothing skip the copy altogether.
        struct BuiltWindow { UInt32 WindowID; ImGuiContext* Context; };
        DynamicArray<BuiltWindow> builtWindows;
        UInt64 imguiHash = 0;
        // A held button or key produces no further OS events, yet drags and ImGui's own key repeat
        // advance every frame. Likewise a focused text field: its cursor blinks on a timer.
        bool heldInput = false;
        bool wantsTextInput = false;
        for (EditorWindowInfo* windowInfo : windowsThisFrame) {
            TUsePointer<IWindow> window = mApplicationInfo.AppWindowsManager->GetWindow(windowInfo->WindowID);
            // Not created yet (requested this frame, appears at the top of the next one).
            if (!window) continue;
            ImGuiContext* windowCtx = window->GetImGuiContext();
            if (!windowCtx) continue;
            // A window on its way out keeps its context for another frame or two (the render thread
            // has to let go first) but must not get any more frames built for it.
            if (mApplicationInfo.AppWindowsManager->IsWindowClosing(windowInfo->WindowID)) continue;

            ImGui::SetCurrentContext(windowCtx);
#ifdef PLU_PLATFORM_WINDOWS
            ImGui_ImplWin32_NewFrame();
#elif defined(PLU_PLATFORM_LINUX)
            ImGui_ImplSDL3_NewFrame();
#endif
            ImGui::NewFrame();
            // Musi lecieć raz na klatkę, po NewFrame, zanim którykolwiek panel woła ImGuizmo::Manipulate.
            ImGuizmo::BeginFrame();

            OnImGuiRenderForWindow(*windowInfo);

            // Feed the window hit-test (SDL/Win32 drag handling): when an ImGui item is hovered
            // the OS title-bar drag must yield so clicks reach the UI. Previously set on the render
            // thread in Renderer.cpp; that path is gone with the ImGui snapshot handoff, so refresh
            // it here on the Main thread (same thread the hit-test callback runs on).
            window->ImGuiItemHovered = ImGui::IsAnyItemHovered();
            builtWindowCount++;
            if (ImGui::IsAnyMouseDown() || IsAnyImGuiKeyDown()) heldInput = true;
            if (ImGui::GetIO().WantTextInput) wantsTextInput = true;
            ImGui::Render();
            builtWindows.PushBack(BuiltWindow{window->GetWindowID(), windowCtx});
            imguiHash = HashImGuiDrawData(imguiHash, window->GetWindowID(), ImGui::GetDrawData());
        }
        // Back to the main window's context: everything below (and every caller that does not set
        // the context itself) assumes it is current.
        ImGui::SetCurrentContext(ctx);

        // Did this frame actually leave texture work pending (e.g. a lazily-baked glyph just created
        // a new atlas texture on an otherwise free-running frame)? If so its snapshot MUST be
        // uploaded by the render thread before it draws, so engage lockstep now even if we didn't
        // pre-engage. The mutation already happened during building, but a newly created texture is
        // a fresh object the prior snapshot doesn't reference, so engaging here still prevents the
        // render thread from drawing it un-uploaded. The atlas is shared by every window's context,
        // so window 0's texture list covers them all.
        const bool texturesPending = ImGuiHasPendingTextureWork();
        if (texturesPending && !lockstepEngaged) {
            mApplicationInfo.AppRenderingManager->BeginImGuiLockstep();
            lockstepEngaged = true;
        }

        // Power saving: a probe frame (the pointer moved, nothing else happened) goes to the render
        // thread only if it would look different from what is already on screen — a hover
        // highlight, a tooltip. Lockstep frames always go: the render thread is parked waiting for
        // one, and it carries the texture uploads.
        if (!IsProbeFrame() || lockstepEngaged || imguiHash != mPublishedImGuiHash) {
            mApplicationInfo.AppRenderingManager->BeginImGuiFrameSubmit();
            for (const BuiltWindow& built : builtWindows) {
                ImGui::SetCurrentContext(built.Context);
                mApplicationInfo.AppRenderingManager->SubmitImGuiDrawData(built.WindowID, ImGui::GetDrawData());
            }
            ImGui::SetCurrentContext(ctx);
            mApplicationInfo.AppRenderingManager->EndImGuiFrameSubmit();
            mPublishedImGuiHash = imguiHash;
        }

        if (lockstepEngaged) {
            // Hand this snapshot to the render thread for exactly one upload+draw pass, then re-scan:
            // uploaded textures are now OK, but a destroy can still be in flight (UnusedFrames-gated),
            // so stay engaged until everything is back at ImTextureStatus_OK, then resume the normal
            // lock-free free-running handoff.
            mApplicationInfo.AppRenderingManager->StepImGuiLockstep();
            mImGuiAtlasSettling = ImGuiHasPendingTextureWork();
            if (!mImGuiAtlasSettling) {
                mApplicationInfo.AppRenderingManager->EndImGuiLockstep();
            }
        } else {
            mImGuiAtlasSettling = false;
        }

        // Power saving: things that need the next frame although no OS event will announce it.
        // The atlas is still settling, or a window is still on its way in or out (both are
        // multi-frame handshakes with the render thread).
        bool needsNextFrame = mImGuiAtlasSettling || windowsThisFrame.Size() != builtWindowCount;
        // An import runs a step per frame.
        if (mAssetImporter) needsNextFrame = true;
        if (needsNextFrame || heldInput) RequestContinuousRedraw();
        if (wantsTextInput) RequestRedrawAfter(kTextCursorBlinkSeconds);
    }
}

void Plu::PluEditor::OnIdleTick(float deltaTime)
{
    PollFileWatchers(deltaTime);
}

bool Plu::PluEditor::WantsContinuousFrames()
{
    // The game ticks and renders every frame; there is no way to redraw the viewport alone.
    return mEditorAppContext->EditorScenesManager->IsInPIE();
}

void Plu::PluEditor::PollFileWatchers(float deltaTime)
{
    mFileWatchTimer += deltaTime;
    if (mFileWatchTimer < kFileWatchIntervalSeconds) return;
    mFileWatchTimer = 0.0f;
    mEditorAppContext->EditorShaderManager->CheckForShaderChanges();
    mEditorAppContext->EditorPythonManager->CheckForScriptsChanges();
}

void Plu::PluEditor::OnRequestedGameExit()
{
    if (!mEditorAppContext->EditorScenesManager->IsInPIE()) return;
    mEditorAppContext->EditorScenesManager->ExitPIE();
    EndGame();
    gApplicationInfo->AppWindow->UpdateImGui = true;
    mApplicationInfo.AppWindow->SetCursorVisibility(true);
}

void Plu::PluEditor::OnRequestedWindowClose(TUsePointer<IWindow> window)
{
    // Save the layout right here, while every window is still standing. Quitting usually arrives as
    // a close request to *all* windows at once (SDL_EVENT_QUIT, or the window manager's "close all
    // windows"), and the secondary ones have their records destroyed on the next frame — by the
    // time OnShutdown() saves, the layout would describe the main window alone. Saving again on
    // shutdown still happens; this is just the earliest point where the picture is complete.
    mEditorAppContext->EditorWindowsManager->SaveLayout();
    mLayoutSavedOnQuit = true;

    if (mApplicationInfo.AppAssetManager->AreAnyAssetsDirty() && !mArgumentParser->get<bool>("debug")) {
        mAssetSaveConfirmShow = true;
        return;
    }
    DispatchWindowClose(window);
}


