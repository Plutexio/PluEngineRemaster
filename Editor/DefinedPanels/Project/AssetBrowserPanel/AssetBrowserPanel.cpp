//
// Created by Plutex on 2026-03-28.
//

#include "AssetBrowserPanel.h"

#include <cstring>
#include <filesystem>

#include "EditorAppContext.h"
#include "imgui_stdlib.h"
#include "nfd.h"
#include "EditorViewports/EditorViewportManager.h"
#include "Managers/Assets/EditorAssetCreator.h"
#include "Managers/Assets/EditorAssetImporter.h"
#include "Managers/Assets/EditorAssetManager.h"
#include "Managers/Project/EditorProjectManager.h"
#include "PluEngine/Application.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/AssetLoader.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/Gameplay/InputManager.h"
#include "PluEngine/Core/IAssetData.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Core/Reflection/ClassPointer.h"
#include "UI/IconsFontAwesome7.h"

namespace
{
    // Drag & drop payload carrying the UTF-8 path of the dragged file or folder.
    constexpr const char* CBrowserDragPayload = "PLU_BROWSER_ENTRY";

    constexpr const char* CDirectoryLabels[] = { "Assets", "Scripts", "Shaders", "Engine Assets" };

    Plu::Path ToNarrowPath(const Plu::PathW& path)
    {
        return Plu::Path(path.ToString().ToNarrow());
    }

    // Characters no file system of ours accepts inside a single name component.
    bool IsNameCharacterInvalid(char c)
    {
        return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
               c == '"' || c == '<' || c == '>' || c == '|' || static_cast<unsigned char>(c) < 32;
    }

    bool PathExistsOnDisk(const Plu::PathW& path)
    {
        std::error_code error;
        return std::filesystem::exists(std::filesystem::path(path.CStr()), error);
    }

    // "NewFolder", "NewFolder1", "NewFolder2", … — first name free inside parentDirectory.
    Plu::String MakeUniqueFolderName(const Plu::PathW& parentDirectory)
    {
        Plu::String candidate = "NewFolder";
        int suffix = 1;
        while (PathExistsOnDisk(Plu::PathW(parentDirectory).Append(Plu::PathW(candidate.ToWide()))))
        {
            candidate = Plu::String("NewFolder") + Plu::String::FromInt(suffix);
            ++suffix;
        }
        return candidate;
    }
}

Plu::AssetBrowserPanel::AssetBrowserPanel()
{
    mUUID = PluUUID();
}

Plu::AssetBrowserPanel::~AssetBrowserPanel() = default;

Plu::String Plu::AssetBrowserPanel::GetPanelName()
{
	switch (mActiveDirectory) {
	    case EAssetDirectory::EngineAssets:
        case EAssetDirectory::Assets:
	        return ICON_FA_FOLDER_CLOSED " Assets Browser###AssetBrowser";
        case EAssetDirectory::Scripts:
	        return ICON_FA_FOLDER_CLOSED " Scripts Browser###AssetBrowser";
        case EAssetDirectory::Shaders:
	        return ICON_FA_FOLDER_CLOSED " Shaders Browser###AssetBrowser";
    }
    return "Content Browser";
}

void Plu::AssetBrowserPanel::OnUpdate(float deltaTime)
{
    if (!BeginPanel()) { EndPanel(); return; }
    if (!mEditorAppContext->EditorProjectManager->IsAnyProjectOpen())
    {
        ImGui::Text("Open project before browsing Assets!");
        EndPanel();
        return;
    }

    if (mCurrentPath.IsEmpty())
        mCurrentPath = GetRootPath();

    // Mouse back/forward buttons do what the ← → toolbar buttons do. Hover-gated, so they only
    // react when the cursor is over this panel; the default hover flags also ignore them while a
    // modal dialog is up.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && mApplicationInfo->AppInputManager)
    {
        const MouseState& mouse = mApplicationInfo->AppInputManager->GetInputBackend()->GetMouse();
        if (mouse.JustPressed(MouseButton::Extra1)) NavigateBack();
        if (mouse.JustPressed(MouseButton::Extra2)) NavigateForward();
    }

    DrawToolbar();
    DrawBreadcrumb();

    ImGui::Separator();
    ImGui::Spacing();

    // The browsed directory can vanish under us (deleted outside the editor) — fall back to root.
    if (!PathExistsOnDisk(mCurrentPath))
    {
        mCurrentPath = GetRootPath();
        mNavigationHistory.Clear();
        mForwardHistory.Clear();
        mSelectedPath = PathW();
    }

    WalkDirectory(mCurrentPath);
    DrawEmptyAreaContextMenu();

    DrawTypeSelectionPopup();
    DrawDialogs();

    // Destroying either of these from their "Finito" callback would free the object (and the
    // callback itself) while its RenderUI() is still running — hence the deferred destruction.
    if (mAssetCreator)
    {
        if (!mAssetCreatorFinished) mAssetCreator->RenderUI();
        if (mAssetCreatorFinished)
        {
            mApplicationInfo->AppObjectManager->DestroyObject(*mAssetCreator->GetEngineObjectHandle());
            mAssetCreator         = nullptr;
            mAssetCreatorFinished = false;
        }
    }
    if (mAssetImporter)
    {
        if (!mAssetImporterFinished) mAssetImporter->RenderUI();
        if (mAssetImporterFinished)
        {
            mApplicationInfo->AppObjectManager->DestroyObject(*mAssetImporter->GetEngineObjectHandle());
            mAssetImporter         = nullptr;
            mAssetImporterFinished = false;
        }
    }

    EndPanel();
}

// ── toolbar & navigation ─────────────────────────────────────────────────────

Plu::PathW Plu::AssetBrowserPanel::GetRootPath() const
{
    switch (mActiveDirectory)
    {
        case EAssetDirectory::Scripts:      return mEditorAppContext->EditorProjectManager->GetProjectScriptsDirectory();
        case EAssetDirectory::Shaders:      return mEditorAppContext->EditorProjectManager->GetProjectShadersDirectory();
        case EAssetDirectory::EngineAssets: return mEditorAppContext->EditorProjectManager->GetEngineAssetsPath();
        default:                            return mEditorAppContext->EditorProjectManager->GetProjectAssetsDirectory();
    }
}

bool Plu::AssetBrowserPanel::IsDirectoryReadOnly() const
{
    return mActiveDirectory == EAssetDirectory::EngineAssets;
}

void Plu::AssetBrowserPanel::NavigateTo(const PathW& path)
{
    if (path == mCurrentPath) return;
    mNavigationHistory.PushBack(mCurrentPath);
    mForwardHistory.Clear();   // new branch — whatever was ahead is gone, like in a browser
    mCurrentPath  = path;
    mSelectedPath = PathW();
}

void Plu::AssetBrowserPanel::NavigateBack()
{
    if (mNavigationHistory.IsEmpty()) return;
    mForwardHistory.PushBack(mCurrentPath);
    mCurrentPath = mNavigationHistory.Back();
    mNavigationHistory.PopBack();
    mSelectedPath = PathW();
}

void Plu::AssetBrowserPanel::NavigateForward()
{
    if (mForwardHistory.IsEmpty()) return;
    mNavigationHistory.PushBack(mCurrentPath);
    mCurrentPath = mForwardHistory.Back();
    mForwardHistory.PopBack();
    mSelectedPath = PathW();
}

void Plu::AssetBrowserPanel::NavigateUp()
{
    if (mCurrentPath == GetRootPath()) return;
    NavigateTo(mCurrentPath.GetParentPath());
}

void Plu::AssetBrowserPanel::DrawToolbar()
{
    const bool readOnly = IsDirectoryReadOnly();

    if (ImGui::Button(ICON_FA_PLUS))
        ImGui::OpenPopup("AssetBrowserAdd");
    ImGui::SetItemTooltip("New folder / import / create asset");

    if (ImGui::BeginPopup("AssetBrowserAdd"))
    {
        ImGui::BeginDisabled(readOnly);
        if (ImGui::Selectable(ICON_FA_FOLDER_PLUS " New Folder"))
            OpenDialog(EBrowserDialog::NewFolder, mCurrentPath, true);
        ImGui::Separator();
        if (ImGui::Selectable(ICON_FA_FILE_IMPORT " Import Asset(s)"))
            BeginAssetImport(mCurrentPath);
        if (ImGui::Selectable(ICON_FA_FILE_CIRCLE_PLUS " Create Asset"))
            BeginAssetTypeSelection(mCurrentPath);
        ImGui::EndDisabled();
        if (readOnly)
            ImGui::TextDisabled("Engine assets are read-only");
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    int activeIndex = static_cast<int>(mActiveDirectory);
    ImGui::SetNextItemWidth(110.0f * ImGui::GetFontSize() / 13);
    if (ImGui::Combo("##dirtype", &activeIndex, CDirectoryLabels, IM_ARRAYSIZE(CDirectoryLabels)))
    {
        mActiveDirectory = static_cast<EAssetDirectory>(activeIndex);
        mCurrentPath     = GetRootPath();
        mSelectedPath    = PathW();
        mNavigationHistory.Clear();
        mForwardHistory.Clear();
    }

    ImGui::SameLine();

    const bool canGoBack = !mNavigationHistory.IsEmpty();
    if (!canGoBack) ImGui::BeginDisabled();
    if (ImGui::ArrowButton("##back", ImGuiDir_Left))
        NavigateBack();
    if (!canGoBack) ImGui::EndDisabled();

    ImGui::SameLine();

    const bool canGoForward = !mForwardHistory.IsEmpty();
    if (!canGoForward) ImGui::BeginDisabled();
    if (ImGui::ArrowButton("##forward", ImGuiDir_Right))
        NavigateForward();
    if (!canGoForward) ImGui::EndDisabled();

    ImGui::SameLine();

    const bool canGoUp = mCurrentPath != GetRootPath();
    if (!canGoUp) ImGui::BeginDisabled();
    if (ImGui::ArrowButton("##up", ImGuiDir_Up))
        NavigateUp();
    if (!canGoUp) ImGui::EndDisabled();
    // Dropping onto the up arrow moves the entry into the parent folder.
    if (canGoUp && ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(CBrowserDragPayload))
            MoveEntry(PathW(StringW::FromNarrow(static_cast<const char*>(payload->Data))), mCurrentPath.GetParentPath());
        ImGui::EndDragDropTarget();
    }
}

void Plu::AssetBrowserPanel::DrawBreadcrumb()
{
    const PathW root = GetRootPath();

    DynamicArray<PathW>  segmentPaths;
    DynamicArray<String> segmentNames;
    segmentPaths.PushBack(root);
    segmentNames.PushBack(CDirectoryLabels[static_cast<int>(mActiveDirectory)]);

    const StringW& current = mCurrentPath.ToString();
    const StringW& rootStr = root.ToString();
    if (current.Length() > rootStr.Length() && current.StartsWith(rootStr.CStr()))
    {
        PathW accumulated = root;
        for (const StringW& part : current.Substring(rootStr.Length() + 1).Split(PathW::PreferredSeparator))
        {
            if (part.IsEmpty()) continue;
            accumulated.Append(PathW(part));
            segmentPaths.PushBack(accumulated);
            segmentNames.PushBack(part.ToNarrow());
        }
    }

    for (UInt64 i = 0; i < segmentPaths.Size(); ++i)
    {
        ImGui::SameLine(0.0f, 4.0f);
        if (i > 0)
        {
            ImGui::TextDisabled(ICON_FA_ANGLE_RIGHT);
            ImGui::SameLine(0.0f, 4.0f);
        }

        ImGui::PushID(static_cast<int>(i));
        const bool isLast = (i + 1 == segmentPaths.Size());
        if (isLast) ImGui::BeginDisabled();
        if (ImGui::SmallButton(segmentNames[i].CStr()))
            NavigateTo(segmentPaths[i]);
        if (isLast) ImGui::EndDisabled();

        if (!isLast && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(CBrowserDragPayload))
                MoveEntry(PathW(StringW::FromNarrow(static_cast<const char*>(payload->Data))), segmentPaths[i]);
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
    }
}

// ── directory listing ────────────────────────────────────────────────────────

void Plu::AssetBrowserPanel::WalkDirectory(const PathW& dirPath)
{
    constexpr float CItemSize = 80.0f;
    constexpr float CPadding  = 6.0f;

    float ItemSize = CItemSize * ImGui::GetFontSize() / 13;
    float Padding  = CPadding * ImGui::GetFontSize() / 13;

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const int   columns        = std::max(1, static_cast<int>(availableWidth / (ItemSize + Padding)));

    // Collect first — directory_iterator gives no ordering guarantees, and the tiles must not
    // jump around between frames.
    DynamicArray<PathW> directories;
    DynamicArray<PathW> files;

    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(dirPath.CStr()), error))
    {
        PathW entryPath(entry.path().wstring().c_str());
        if (entry.is_directory(error)) directories.PushBack(entryPath);
        else                           files.PushBack(entryPath);
    }

    auto ByName = [](const PathW& a, const PathW& b)
    {
        const String left  = a.GetFilename().ToNarrow().ToLower();
        const String right = b.GetFilename().ToNarrow().ToLower();
        return std::strcmp(left.CStr(), right.CStr()) < 0;
    };
    directories.Sort(ByName);
    files.Sort(ByName);

    if (directories.IsEmpty() && files.IsEmpty())
    {
        ImGui::TextDisabled("This folder is empty");
        return;
    }

    int col = 0;
    auto DrawAndAdvance = [&](const PathW& path, bool isDir)
    {
        DrawAssetItem(path, isDir);

        ++col;
        if (col < columns)
            ImGui::SameLine(0.0f, Padding);
        else
        {
            col = 0;
            ImGui::Spacing();
        }
    };

    for (const PathW& directory : directories) DrawAndAdvance(directory, true);
    for (const PathW& file : files)            DrawAndAdvance(file, false);

    // The layout above ends with a SameLine when the last row is not full — close it so the
    // empty-area context menu hit-tests against the remaining space.
    if (col != 0) ImGui::NewLine();
}

// Breaks the label per character (asset names rarely contain spaces) into at most maxLines lines
// fitting maxWidth; the last line gets "..." when the rest does not fit.
static DynamicArray<std::string> WrapAssetLabel(const char* text, float maxWidth, int maxLines)
{
    DynamicArray<std::string> lines;
    std::string remaining(text);

    while (!remaining.empty() && static_cast<int>(lines.Size()) < maxLines)
    {
        const bool isLastLine = (static_cast<int>(lines.Size()) + 1 == maxLines);

        size_t fitChars = remaining.size();
        while (fitChars > 0 && ImGui::CalcTextSize(remaining.substr(0, fitChars).c_str()).x > maxWidth)
            --fitChars;

        if (fitChars == remaining.size())
        {
            lines.PushBack(remaining);
            remaining.clear();
        }
        else if (isLastLine)
        {
            const std::string ellipsis = "...";
            size_t truncChars = fitChars;
            while (truncChars > 0 &&
                   ImGui::CalcTextSize((remaining.substr(0, truncChars) + ellipsis).c_str()).x > maxWidth)
                --truncChars;
            lines.PushBack(remaining.substr(0, truncChars) + ellipsis);
            remaining.clear();
        }
        else
        {
            // unikaj nieskończonej pętli, gdy nawet jeden znak się nie mieści
            fitChars = std::max<size_t>(fitChars, 1);
            lines.PushBack(remaining.substr(0, fitChars));
            remaining = remaining.substr(fitChars);
        }
    }

    return lines;
}

void Plu::AssetBrowserPanel::DrawAssetItem(const PathW& path, bool isDirectory)
{
    constexpr float CItemSize    = 80.0f;
    constexpr float CIconSize    = 44.0f;
    constexpr float CLabelLineHeight = 14.0f;
    constexpr int   CMaxLabelLines   = 2;
    constexpr float CPadding     = 6.0f;

    float fontSizeMultiplier = ImGui::GetFontSize() / 13;

    float ItemSize       = CItemSize * fontSizeMultiplier;
    float IconSize       = CIconSize * fontSizeMultiplier;
    float LabelLineHeight = CLabelLineHeight * fontSizeMultiplier;
    float Padding        = CPadding * fontSizeMultiplier;

    // ── label wrapped to at most CMaxLabelLines lines. Folders keep their full name (dots in a
    // folder name are not an extension), files show the stem only. ──────────────────────────────
    const String label = isDirectory ? path.GetFilename().ToNarrow() : path.GetStem().ToNarrow();
    DynamicArray<std::string> labelLines = WrapAssetLabel(label.CStr(), ItemSize - 4.0f, CMaxLabelLines);
    const float labelBlockHeight = LabelLineHeight * static_cast<float>(labelLines.Size());

    const bool isSelected = (path == mSelectedPath);

    ImGui::PushID(path.ToString().ToNarrow().CStr());

    // ── item bounding box (grows when the label takes more than one line) ─────────────────────
    ImVec2      itemMin  = ImGui::GetCursorScreenPos();
    ImVec2      itemMax  = ImVec2(itemMin.x + ItemSize, itemMin.y + ItemSize + (labelBlockHeight - LabelLineHeight));
    ImDrawList* draw     = ImGui::GetWindowDrawList();
    const bool  hovered  = ImGui::IsMouseHoveringRect(itemMin, itemMax);

    // ── background ────────────────────────────────────────────────────────────
    if (isSelected)
    {
        draw->AddRectFilled(itemMin, itemMax,
            IM_COL32(50, 110, 200, 70), 6.0f * fontSizeMultiplier);
        draw->AddRect(itemMin, itemMax,
            IM_COL32(80, 150, 255, 220), 6.0f * fontSizeMultiplier, 0, 1.5f);
    }
    else if (hovered)
    {
        draw->AddRectFilled(itemMin, itemMax,
            ImGui::ColorConvertFloat4ToU32(
                ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered)), 6.0f * fontSizeMultiplier);
    }

    // ── invisible button ─────────────────────────────────────────────────────
    ImGui::InvisibleButton("##item", ImVec2(itemMax.x - itemMin.x, itemMax.y - itemMin.y));

    // ── drag & drop: any entry can be dragged, folders accept drops ───────────
    if (!IsDirectoryReadOnly() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
    {
        const String payloadPath = path.ToString().ToNarrow();
        ImGui::SetDragDropPayload(CBrowserDragPayload, payloadPath.CStr(), payloadPath.Length() + 1);
        ImGui::Text("%s %s", isDirectory ? ICON_FA_FOLDER : ICON_FA_FILE, label.CStr());
        ImGui::EndDragDropSource();
    }
    if (isDirectory && ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(CBrowserDragPayload))
            MoveEntry(PathW(StringW::FromNarrow(static_cast<const char*>(payload->Data))), path);
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        OnAssetClicked(path, isDirectory);

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        OnAssetDoubleClicked(path, isDirectory);

    if (ImGui::BeginPopupContextItem("##ctx"))
    {
        OnAssetContextMenu(path, isDirectory);
        ImGui::EndPopup();
    }

    // ── icon ──────────────────────────────────────────────────────────────────
    const float iconOffX = itemMin.x + (ItemSize - IconSize) * 0.5f;
    const float iconOffY = itemMin.y + 6.0f;

    if (isDirectory)
    {
        // folder — simple shape with a tab
        draw->AddRectFilled(
            ImVec2(iconOffX,          iconOffY + 8.0f),
            ImVec2(iconOffX + IconSize, iconOffY + IconSize),
            IM_COL32(200, 160, 60, 200), 4.0f);
        draw->AddRectFilled(
            ImVec2(iconOffX,          iconOffY + 2.0f),
            ImVec2(iconOffX + IconSize * 0.5f, iconOffY + 12.0f),
            IM_COL32(200, 160, 60, 200), 2.0f);
    }
    else
    {
        // file — flat colour; put draw->AddImage(texID, ...) here once thumbnails exist
        const bool hasAsset = mEditorAppContext->EditorAssetManager
                                  ->AssetExistsInPath(ToNarrowPath(path));
        const ImU32 iconColor = hasAsset
            ? IM_COL32(80, 120, 200, 180)   // known asset — blue
            : IM_COL32(80,  80, 100, 120);  // unknown — grey

        draw->AddRectFilled(
            ImVec2(iconOffX, iconOffY),
            ImVec2(iconOffX + IconSize, iconOffY + IconSize),
            iconColor, 4.0f * fontSizeMultiplier);
    }

    // ── label (one or two wrapped, centred lines) ────────────────────────────
    float labelY = itemMin.y + (itemMax.y - itemMin.y) - labelBlockHeight - 4.0f;

    for (const std::string& line : labelLines)
    {
        const float textWidth = ImGui::CalcTextSize(line.c_str()).x;
        const float labelX    = itemMin.x + std::max(0.0f, (ItemSize - textWidth) * 0.5f);

        draw->AddText(
            ImGui::GetFont(),
            ImGui::GetFontSize() - 1.0f,
            ImVec2(labelX, labelY),
            isSelected ? IM_COL32(140, 200, 255, 255)
                       : IM_COL32(180, 180, 180, 255),
            line.c_str());

        labelY += LabelLineHeight;
    }

    ImGui::PopID();
}

void Plu::AssetBrowserPanel::DrawEmptyAreaContextMenu()
{
    if (!ImGui::BeginPopupContextWindow("##browserctx",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        return;

    if (IsDirectoryReadOnly())
    {
        ImGui::TextDisabled("Engine assets are read-only");
        ImGui::EndPopup();
        return;
    }

    if (ImGui::MenuItem(ICON_FA_FOLDER_PLUS " New Folder"))
        OpenDialog(EBrowserDialog::NewFolder, mCurrentPath, true);
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_FILE_IMPORT " Import Asset(s) Here"))
        BeginAssetImport(mCurrentPath);
    if (ImGui::MenuItem(ICON_FA_FILE_CIRCLE_PLUS " Create Asset Here"))
        BeginAssetTypeSelection(mCurrentPath);

    ImGui::EndPopup();
}

// ── asset creation / import ──────────────────────────────────────────────────

void Plu::AssetBrowserPanel::BeginAssetImport(const PathW& targetDirectory)
{
    const nfdpathset_t* outPaths = nullptr;
    const nfdu8filteritem_t filters[1] = { { "Asset Files", "fbx,obj,png,jpg,glb,gltf" } }; //TODO modular extension from loaders
    if (NFD_OpenDialogMultipleU8(&outPaths, filters, 1, nullptr) != NFD_OKAY) return;

    nfdpathsetsize_t numPaths = 0;
    NFD_PathSet_GetCount(outPaths, &numPaths);
    DynamicArray<Path> assetPaths;
    for (nfdpathsetsize_t i = 0; i < numPaths; ++i) {
        nfdu8char_t* path = nullptr;
        NFD_PathSet_GetPathU8(outPaths, i, &path);
        assetPaths.PushBack(Path(path));
        NFD_PathSet_FreePathU8(path);
    }
    NFD_PathSet_Free(outPaths);

    if (assetPaths.IsEmpty() || mAssetImporter) return;

    mAssetImporter = mApplicationInfo->AppObjectManager->CreateObject(EditorAssetImporter::GetStaticClass());
    mAssetImporterFinished = false;
    mAssetImporter->GetObjectEventDispatcher()->Subscribe("Finito", [this](void*) {
        mAssetImporterFinished = true;
    });
    mAssetImporter->Initialize(assetPaths, mApplicationInfo, ToNarrowPath(targetDirectory));
}

void Plu::AssetBrowserPanel::BeginAssetTypeSelection(const PathW& targetDirectory)
{
    mAssetTypesForCreation.Clear();
    for (auto type : *TypeRegistry::GetInstance()->GetTypeMap()) {
        if (!type.second->IsDerivedOf(IAssetData::GetStaticClass())) continue;
        if (!mApplicationInfo->AppAssetManager->GetAssetLoader(type.second) || mApplicationInfo->AppAssetManager->GetAssetLoader(type.second)->IsAssetCreatable())
        {
            mAssetTypesForCreation.PushBack(type.second);
        }
    }
    if (mAssetTypesForCreation.IsEmpty()) return;

    mCreationTargetDirectory = targetDirectory;
    mOpenTypeSelection       = true;
}

void Plu::AssetBrowserPanel::DrawTypeSelectionPopup()
{
    if (mOpenTypeSelection) {
        ImGui::OpenPopup("Asset Creator: Type Selection");
        mOpenTypeSelection = false;
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5,0.5));
        ImGui::SetNextWindowSize(ImVec2(350, 500), ImGuiCond_Always);
    }

    if (!ImGui::BeginPopupModal("Asset Creator: Type Selection")) return;

    ImGui::TextDisabled("In: %s", mCreationTargetDirectory.GetFilename().ToNarrow().CStr());
    ImGui::Separator();

    for (auto type : mAssetTypesForCreation) {
        String name;
        if (type->TypeName.EndsWith("Info"))
        {
            name = type->TypeName;
            name.Remove(name.Length() - 4);
        } else
        {
            name = type->TypeName;
        }
        if (ImGui::Selectable(MakeStringForDisplay(name).CStr()))
        {
            if (!mAssetCreator) {
                mAssetCreator = mApplicationInfo->AppObjectManager->CreateObject(EditorAssetCreator::GetStaticClass());
                mAssetCreatorFinished = false;
                mAssetCreator->Initialize(type, mApplicationInfo->AppAssetManager, mCreationTargetDirectory);
                mAssetCreator->GetObjectEventDispatcher()->Subscribe("Finito", [this](void*) {
                    mAssetCreatorFinished = true;
                });
            }
            // The creator opens its own modal — this one has done its job.
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ── folder / file operations ─────────────────────────────────────────────────

void Plu::AssetBrowserPanel::OpenDialog(EBrowserDialog dialog, const PathW& targetPath, bool targetIsDirectory)
{
    mPendingDialog         = dialog;
    mOpenPendingDialog     = true;
    mDialogPath            = targetPath;
    mDialogPathIsDirectory = targetIsDirectory;
    mDialogError           = String();

    switch (dialog)
    {
        case EBrowserDialog::NewFolder: mDialogNameBuffer = MakeUniqueFolderName(targetPath).CStr(); break;
        case EBrowserDialog::Rename:    mDialogNameBuffer = (targetIsDirectory ? targetPath.GetFilename() : targetPath.GetStem()).ToNarrow().CStr(); break;
        default:                        mDialogNameBuffer.clear(); break;
    }
}

bool Plu::AssetBrowserPanel::ValidateDialogName(const PathW& parentDirectory, const PathW& originalPath)
{
    const String name = mDialogNameBuffer.c_str();
    if (name.IsEmpty())
    {
        mDialogError = "Name cannot be empty";
        return false;
    }
    for (UInt64 i = 0; i < name.Length(); ++i)
    {
        if (IsNameCharacterInvalid(name[i]))
        {
            mDialogError = "Name contains invalid characters";
            return false;
        }
    }
    if (name == "." || name == "..")
    {
        mDialogError = "Invalid name";
        return false;
    }

    // Renaming a file keeps its extension, so the on-disk name gets it appended back.
    StringW fileName = name.ToWide();
    if (!originalPath.IsEmpty() && !mDialogPathIsDirectory)
        fileName += originalPath.GetExtension();

    const PathW candidate = PathW(parentDirectory).Append(PathW(fileName));
    if (candidate != originalPath && PathExistsOnDisk(candidate))
    {
        mDialogError = "An entry with this name already exists";
        return false;
    }

    mDialogError = String();
    return true;
}

void Plu::AssetBrowserPanel::CommitNewFolder()
{
    if (!ValidateDialogName(mDialogPath, PathW())) return;

    const PathW newFolder = PathW(mDialogPath).Append(PathW(StringW::FromNarrow(mDialogNameBuffer.c_str())));

    std::error_code error;
    std::filesystem::create_directory(std::filesystem::path(newFolder.CStr()), error);
    if (error)
    {
        mDialogError = String("Could not create folder: ") + error.message().c_str();
        return;
    }

    mSelectedPath  = newFolder;
    mPendingDialog = EBrowserDialog::None;
    ImGui::CloseCurrentPopup();
}

void Plu::AssetBrowserPanel::CommitRename()
{
    const PathW parent = mDialogPath.GetParentPath();
    if (!ValidateDialogName(parent, mDialogPath)) return;

    StringW fileName = StringW::FromNarrow(mDialogNameBuffer.c_str());
    if (!mDialogPathIsDirectory) fileName += mDialogPath.GetExtension();
    const PathW newPath = PathW(parent).Append(PathW(fileName));

    if (newPath == mDialogPath)
    {
        mPendingDialog = EBrowserDialog::None;
        ImGui::CloseCurrentPopup();
        return;
    }

    std::error_code error;
    std::filesystem::rename(std::filesystem::path(mDialogPath.CStr()), std::filesystem::path(newPath.CStr()), error);
    if (error)
    {
        mDialogError = String("Could not rename: ") + error.message().c_str();
        return;
    }

    // The files moved — the asset registry still points at the old paths.
    mApplicationInfo->AppAssetManager->RelocateAssets(ToNarrowPath(mDialogPath), ToNarrowPath(newPath));

    if (mCurrentPath == mDialogPath) mCurrentPath = newPath;
    mSelectedPath  = newPath;
    mPendingDialog = EBrowserDialog::None;
    ImGui::CloseCurrentPopup();
}

void Plu::AssetBrowserPanel::CommitDelete()
{
    std::error_code error;
    if (mDialogPathIsDirectory)
        std::filesystem::remove_all(std::filesystem::path(mDialogPath.CStr()), error);
    else
        std::filesystem::remove(std::filesystem::path(mDialogPath.CStr()), error);

    if (error)
    {
        mDialogError = String("Could not delete: ") + error.message().c_str();
        return;
    }

    if (mSelectedPath == mDialogPath) mSelectedPath = PathW();
    mPendingDialog = EBrowserDialog::None;
    ImGui::CloseCurrentPopup();
}

void Plu::AssetBrowserPanel::MoveEntry(const PathW& sourcePath, const PathW& targetDirectory)
{
    if (IsDirectoryReadOnly()) return;
    if (sourcePath.IsEmpty() || targetDirectory.IsEmpty()) return;
    if (sourcePath.GetParentPath() == targetDirectory) return;

    // Dropping a folder into itself (or into its own subtree) would delete it on most platforms.
    const StringW& sourceStr = sourcePath.ToString();
    const StringW& targetStr = targetDirectory.ToString();
    if (targetStr == sourceStr ||
        (targetStr.Length() > sourceStr.Length() && targetStr.StartsWith(sourceStr.CStr())))
    {
        PLU_WARN("Cannot move a folder into itself");
        return;
    }

    const PathW destination = PathW(targetDirectory).Append(PathW(sourcePath.GetFilename()));
    if (PathExistsOnDisk(destination))
    {
        PLU_WARN("Cannot move {} — target already contains an entry with that name",
                 sourcePath.GetFilename().ToNarrow().CStr());
        return;
    }

    std::error_code error;
    std::filesystem::rename(std::filesystem::path(sourcePath.CStr()), std::filesystem::path(destination.CStr()), error);
    if (error)
    {
        PLU_ERROR("Could not move {}: {}", sourcePath.ToString().ToNarrow().CStr(), error.message().c_str());
        return;
    }

    mApplicationInfo->AppAssetManager->RelocateAssets(ToNarrowPath(sourcePath), ToNarrowPath(destination));

    if (mSelectedPath == sourcePath) mSelectedPath = destination;
}

void Plu::AssetBrowserPanel::DrawDialogs()
{
    constexpr const char* CNewFolderTitle = "New Folder###AssetBrowserDialog";
    constexpr const char* CRenameTitle    = "Rename###AssetBrowserDialog";
    constexpr const char* CDeleteTitle    = "Delete###AssetBrowserDialog";

    const char* title = nullptr;
    switch (mPendingDialog)
    {
        case EBrowserDialog::NewFolder: title = CNewFolderTitle; break;
        case EBrowserDialog::Rename:    title = CRenameTitle;    break;
        case EBrowserDialog::Delete:    title = CDeleteTitle;    break;
        default: return;
    }

    if (mOpenPendingDialog)
    {
        ImGui::OpenPopup(title);
        mOpenPendingDialog = false;
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }

    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    bool confirmed = false;
    if (mPendingDialog == EBrowserDialog::Delete)
    {
        ImGui::Text("Delete %s \"%s\"?",
                    mDialogPathIsDirectory ? "folder" : "file",
                    mDialogPath.GetFilename().ToNarrow().CStr());
        ImGui::TextDisabled("This cannot be undone.");
    }
    else
    {
        ImGui::SetNextItemWidth(260.0f * ImGui::GetFontSize() / 13);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        confirmed = ImGui::InputText("##name", &mDialogNameBuffer, ImGuiInputTextFlags_EnterReturnsTrue);
        if (mPendingDialog == EBrowserDialog::Rename && !mDialogPathIsDirectory)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", mDialogPath.GetExtension().ToNarrow().CStr());
        }
    }

    if (!mDialogError.IsEmpty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", mDialogError.CStr());

    ImGui::Separator();
    const char* confirmLabel = (mPendingDialog == EBrowserDialog::Delete) ? "Delete" : "OK";
    if (ImGui::Button(confirmLabel) || confirmed)
    {
        switch (mPendingDialog)
        {
            case EBrowserDialog::NewFolder: CommitNewFolder(); break;
            case EBrowserDialog::Rename:    CommitRename();    break;
            case EBrowserDialog::Delete:    CommitDelete();    break;
            default: break;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        mPendingDialog = EBrowserDialog::None;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// ── callbacks ────────────────────────────────────────────────────────────────

void Plu::AssetBrowserPanel::OnAssetClicked(const PathW& path, bool isDirectory)
{
    mSelectedPath = path;   // IsSelected resolves automatycznie przy następnym frame
}

void Plu::AssetBrowserPanel::OnAssetDoubleClicked(const PathW& path, bool isDirectory)
{
    if (isDirectory)
    {
        NavigateTo(path);
        return;
    }

    if (mEditorAppContext->EditorAssetManager->AssetExistsInPath(ToNarrowPath(path)))
    {
        PLU_PROFILE_SCOPE_LOG("Editor Viewport Open");
        TUsePointer<AssetDescriptor> assetDescriptor = mApplicationInfo->AppAssetManager->GetAssetDescriptor(ToNarrowPath(path));
        auto typeMap = TypeRegistry::GetInstance()->GetTypeMap();
        HashMap<String,TClassPointer<IEditorViewport>> viewportClasses;
        for (auto type : *typeMap) {
            if (type.second->IsDerivedOf(IEditorViewport::GetStaticClass())) {
                viewportClasses.Insert(type.first,type.second);
            }
        }
        String viewportClassName = assetDescriptor->AssetType->TypeName + "Viewport";
        if (!viewportClasses.Contains(viewportClassName)) {
            TUsePointer<IAssetLoader> assetLoader = mApplicationInfo->AppAssetManager->GetAssetLoader(assetDescriptor->AssetType);
            if (assetLoader) {
                if (assetLoader->GetAssetTypeViewportClass()) {
                    mEditorAppContext->EditorViewportManager->CreateViewport(path, assetLoader->GetAssetTypeViewportClass());
                    return;
                }
            }
            PLU_ERROR("No viewport class for {}", assetDescriptor->AssetType->TypeName.CStr());
            return;
        }
        TClassPointer<IEditorViewport> viewportClass = viewportClasses[viewportClassName];
        mEditorAppContext->EditorViewportManager->CreateViewport(path, viewportClass);
    }
}

void Plu::AssetBrowserPanel::OnAssetContextMenu(const PathW& path, bool isDirectory)
{
    const bool readOnly  = IsDirectoryReadOnly();
    const bool isAsset   = mEditorAppContext->EditorAssetManager->AssetExistsInPath(ToNarrowPath(path));

    if (isDirectory)
    {
        if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Open"))
            NavigateTo(path);

        ImGui::BeginDisabled(readOnly);
        if (ImGui::MenuItem(ICON_FA_FOLDER_PLUS " New Folder"))
            OpenDialog(EBrowserDialog::NewFolder, path, true);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_FILE_IMPORT " Import Asset(s) Here"))
            BeginAssetImport(path);
        if (ImGui::MenuItem(ICON_FA_FILE_CIRCLE_PLUS " Create Asset Here"))
            BeginAssetTypeSelection(path);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_PEN " Rename"))
            OpenDialog(EBrowserDialog::Rename, path, true);

        // Removing a folder full of assets would leave the registry pointing at deleted files —
        // asset deletion is not supported yet, so only asset-free folders can go.
        const bool holdsAssets = mApplicationInfo->AppAssetManager->AnyAssetsUnderDirectory(ToNarrowPath(path));
        ImGui::BeginDisabled(holdsAssets);
        if (ImGui::MenuItem(ICON_FA_TRASH " Delete"))
            OpenDialog(EBrowserDialog::Delete, path, true);
        ImGui::EndDisabled();
        if (holdsAssets)
            ImGui::SetItemTooltip("Folder contains assets — deleting assets is not supported yet");
        ImGui::EndDisabled();
        return;
    }

    if (isAsset && ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Open"))
        OnAssetDoubleClicked(path, false);

    ImGui::BeginDisabled(readOnly);
    if (ImGui::MenuItem(ICON_FA_PEN " Rename"))
        OpenDialog(EBrowserDialog::Rename, path, false);

    ImGui::BeginDisabled(isAsset);
    if (ImGui::MenuItem(ICON_FA_TRASH " Delete"))
        OpenDialog(EBrowserDialog::Delete, path, false);
    ImGui::EndDisabled();
    if (isAsset)
        ImGui::SetItemTooltip("Deleting assets is not supported yet");
    ImGui::EndDisabled();
}

void Plu::AssetBrowserPanel::OnHide()
{
}

void Plu::AssetBrowserPanel::OnShow()
{
    SetCanClose(true);
}
