//
// Created by Plutex on 2026-02-20.
//

#include "EditorWindowsManager.h"

#include <filesystem>

#include "EditorAppContext.h"
#include "EditorViewports/EditorViewportManager.h"
#include "EditorViewports/IEditorPanel.h"
#include "EditorViewports/IEditorViewport.h"
#include "Panels/EditorPanelManager.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/AssetLoader.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluBase/Log.h"
#include "Managers/Project/EditorProjectManager.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/Platform/Window.h"
#include "PluEngine/Platform/WindowsManager.h"

extern Plu::TUsePointer<Plu::EngineObjectManager> gEngineObjectManager;
extern Plu::EditorAppContext* gEditorAppContext;
extern Plu::ApplicationInfo* gApplicationInfo;

Plu::String Plu::StripImGuiIDFromName(const String &imguiName)
{
	const size_t idStart = imguiName.Find("##");
	if (idStart == String::Npos) return imguiName;
	if (idStart > 0) return imguiName.Substring(0, idStart);

	// "###Id" — pure id, no label at all (ImGui shows nothing). The id is still the most
	// descriptive thing there is, so show it without the hashes rather than an empty title bar.
	size_t textStart = 0;
	while (textStart < imguiName.Length() && imguiName[textStart] == '#') ++textStart;
	return imguiName.Substring(textStart);
}

Plu::EditorWindowsManager::~EditorWindowsManager()
{
	for (EditorWindowInfo* window : mWindows) delete window;
	mWindows.Clear();
}

void Plu::EditorWindowsManager::Initialize(ApplicationInfo *applicationInfo, EditorAppContext *editorAppContext)
{
	mApplicationInfo = applicationInfo;
	mEditorAppContext = editorAppContext;
}

void Plu::EditorWindowsManager::RegisterMainWindow(const String& title)
{
	if (GetWindowInfo(0)) return;
	EditorWindowInfo* info = new EditorWindowInfo();
	info->WindowID = 0;
	info->Kind = EEditorWindowKind::Main;
	info->Title = title;
	info->WindowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoSplit | ImGuiDockNodeFlags_NoCloseButton | ImGuiDockNodeFlags_NoWindowMenuButton;
	mWindows.PushBack(info);
}

UInt32 Plu::EditorWindowsManager::CreateEditorWindow(EEditorWindowKind kind, const String &title)
{
	WindowProperties props;
	props.Title = StripImGuiIDFromName(title);
	// Borderless like the main window: the editor draws its own title bar and window controls,
	// and SDL's hit test handles dragging and resizing.
	props.Borderless = true;
	props.InitImGui = true;
	props.Width = kind == EEditorWindowKind::SinglePanel ? 600 : 1000;
	props.Height = kind == EEditorWindowKind::SinglePanel ? 450 : 720;

	TUsePointer<IWindow> window = mApplicationInfo->AppWindowsManager->RequestNewWindow(props);
	if (!window) {
		PLU_ERROR("Could not create editor window '{}'", title.CStr());
		return 0;
	}

	EditorWindowInfo* info = new EditorWindowInfo();
	info->WindowID = window->GetWindowID();
	info->Kind = kind;
	info->Title = props.Title;
	info->WindowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoSplit | ImGuiDockNodeFlags_NoCloseButton | ImGuiDockNodeFlags_NoWindowMenuButton;
	mWindows.PushBack(info);

	// The OS close button (and the editor's own close control) only requests a close; turning that
	// into an actual teardown is this manager's job.
	const UInt32 windowID = info->WindowID;
	window->GetObjectEventDispatcher()->Subscribe("WindowCloseRequested", [this, windowID](void*) {
		CloseEditorWindow(windowID);
	});

	PLU_INFO("Editor window {} created ('{}')", windowID, title.CStr());
	return windowID;
}

void Plu::EditorWindowsManager::CloseEditorWindow(UInt32 windowID)
{
	if (windowID == 0) return;
	if (!GetWindowInfo(windowID)) return;
	if (mWindowsToClose.Contains(windowID)) return;
	mWindowsToClose.PushBack(windowID);
}

void Plu::EditorWindowsManager::MoveViewportPanelToWindow(const TUsePointer<IEditorPanel>& panel, UInt32 targetWindowID)
{
	if (!panel) return;
	const UInt32 previousWindowID = panel->GetWindowIDToRender();
	if (previousWindowID == targetWindowID) return;

	// Leaving a window that existed only to host it: that window has nothing left to show.
	if (EditorWindowInfo* previous = GetWindowInfo(previousWindowID)) {
		if (previous->Kind == EEditorWindowKind::SinglePanel && previous->HostedPanel == panel) {
			previous->HostedPanel = nullptr;
			CloseEditorWindow(previousWindowID);
		}
	}

	panel->SetWindowIDToRender(targetWindowID);
	if (EditorWindowInfo* target = GetWindowInfo(targetWindowID)) {
		if (target->Kind == EEditorWindowKind::SinglePanel) {
			target->HostedPanel = panel;
		}
	}
}

void Plu::EditorWindowsManager::DestroyWindowRecord(UInt32 windowID)
{
	for (size_t i = 0; i < mWindows.Size(); ++i) {
		if (mWindows[i]->WindowID != windowID) continue;
		// Everything the window hosted goes home to window 0 — otherwise it would keep rendering
		// into an id nobody builds frames for anymore, i.e. silently disappear.
		if (mEditorAppContext->EditorPanelManager) {
			mEditorAppContext->EditorPanelManager->ReturnPanelsFromWindow(windowID);
		}
		if (mEditorAppContext->EditorViewportManager) {
			// Viewports first, so the panels that follow them land in the window the viewport
			// itself just moved to.
			mEditorAppContext->EditorViewportManager->ReturnViewportsFromWindow(windowID);
			mEditorAppContext->EditorViewportManager->ReturnViewportPanelsFromWindow(windowID);
		}
		delete mWindows[i];
		mWindows.RemoveAt(i);
		PLU_INFO("Editor window {} closed", windowID);
		return;
	}
}

Plu::EditorWindowInfo * Plu::EditorWindowsManager::GetWindowInfo(UInt32 windowID) const
{
	for (EditorWindowInfo* window : mWindows) {
		if (window->WindowID == windowID) return window;
	}
	return nullptr;
}

namespace
{
	// The window layout is a property of the project, not of the editor install — different
	// projects want different windows open. It lives in the project's Config/ directory, together
	// with one ImGui .ini per window (without an explicit path ImGui writes a single imgui.ini into
	// the working directory, which several windows would fight over).
	//
	// Empty path = no project open; nothing is loaded or saved then.
	Plu::PathW GetLayoutDirectory()
	{
		if (!gEditorAppContext || !gEditorAppContext->EditorProjectManager) return Plu::PathW();
		if (!gEditorAppContext->EditorProjectManager->IsAnyProjectOpen()) return Plu::PathW();
		return gEditorAppContext->EditorProjectManager->GetProjectConfigDirectory();
	}

	Plu::PathW GetLayoutJSONPath()
	{
		const Plu::PathW dir = GetLayoutDirectory();
		if (dir.ToString().IsEmpty()) return Plu::PathW();
		std::filesystem::create_directories(dir.CStr());
		return dir / L"EditorWindowsLayout.json";
	}

	Plu::String GetWindowIniPath(size_t slot)
	{
		const Plu::PathW dir = GetLayoutDirectory();
		if (dir.ToString().IsEmpty()) return Plu::String();
		const Plu::PathW layoutDir = dir / L"Layout";
		std::filesystem::create_directories(layoutDir.CStr());
		return (layoutDir / Plu::StringW::FromNarrow(Plu::String::Format("imgui_window_{0}.ini", slot).CStr())).ToString().ToNarrow();
	}
}

void Plu::EditorWindowsManager::SyncImGuiIniFiles()
{
	ImGuiContext* previous = ImGui::GetCurrentContext();
	for (size_t slot = 0; slot < mWindows.Size(); ++slot) {
		EditorWindowInfo* info = mWindows[slot];
		TUsePointer<IWindow> window = mApplicationInfo->AppWindowsManager->GetWindow(info->WindowID);
		if (!window || !window->GetImGuiContext()) continue;
		ImGui::SetCurrentContext(window->GetImGuiContext());

		if (!info->IniPathApplied) {
			info->IniPath = GetWindowIniPath(slot);
			// No project open yet — the docking layout lives in the project, so there is nothing to
			// load from and ImGui keeps its in-memory state until there is.
			if (info->IniPath.IsEmpty()) continue;
			// NOT io.IniFilename: ImGui keeps that as a bare pointer and writes through it whenever
			// it pleases (including from DestroyContext), which outlives this record — a dangling
			// pointer there means an .ini saved under a garbage file name. Driving load/save by
			// call takes the path at the moment of use instead, with no lifetime coupling at all.
			ImGui::GetIO().IniFilename = nullptr;
			ImGui::LoadIniSettingsFromDisk(info->IniPath.CStr());
			info->IniPathApplied = true;
		}
		if (ImGui::GetIO().WantSaveIniSettings) {
			ImGui::SaveIniSettingsToDisk(info->IniPath.CStr());
			ImGui::GetIO().WantSaveIniSettings = false;
		}
	}
	ImGui::SetCurrentContext(previous);
}

void Plu::EditorWindowsManager::SaveLayout()
{
	// Deliberately the project the layout was *loaded* for, not the one open right now: during a
	// project switch the new project is already current, and this save still belongs to the old one.
	const PathW layoutDir = mLayoutProjectDir.ToString().IsEmpty() ? GetLayoutDirectory() : mLayoutProjectDir;
	// No project open: there is nowhere to write this layout, and no project it would describe.
	if (layoutDir.ToString().IsEmpty()) return;
	std::filesystem::create_directories(layoutDir.CStr());
	const PathW layoutPath = layoutDir / L"EditorWindowsLayout.json";

	// Flush each window's docking layout while its context still exists.
	ImGuiContext* previousContext = ImGui::GetCurrentContext();
	for (const EditorWindowInfo* info : mWindows) {
		if (!info->IniPathApplied) continue;
		TUsePointer<IWindow> window = mApplicationInfo->AppWindowsManager->GetWindow(info->WindowID);
		if (!window || !window->GetImGuiContext()) continue;
		ImGui::SetCurrentContext(window->GetImGuiContext());
		ImGui::SaveIniSettingsToDisk(info->IniPath.CStr());
		ImGui::GetIO().WantSaveIniSettings = false;
	}
	ImGui::SetCurrentContext(previousContext);

	// Refresh the mapping from what is actually open, then save the mapping — not the open panels.
	// That way a panel the user moved and later closed still remembers its window next session.
	for (const TOwningPointer<EditorPanel>& panel : mEditorAppContext->EditorPanelManager->GetPanels()) {
		RememberPanelWindow(panel->GetClass()->TypeName, panel->GetWindowIDToRender());
	}

	nlohmann::json layout = nlohmann::json::array();
	for (size_t slot = 0; slot < mWindows.Size(); ++slot) {
		const EditorWindowInfo* info = mWindows[slot];
		TUsePointer<IWindow> window = mApplicationInfo->AppWindowsManager->GetWindow(info->WindowID);
		if (!window) continue;

		nlohmann::json entry;
		// Slot, not WindowID: ids are handed out per session and only stay stable because windows
		// are recreated in this same order — which is also what keeps the .ini files matched up.
		entry["Slot"] = slot;
		entry["Kind"] = static_cast<int>(info->Kind);
		entry["Title"] = info->Title.CStr();
		const IVec2 position = window->GetWindowPosition();
		entry["X"] = position.x;
		entry["Y"] = position.y;
		entry["Width"] = window->GetWidth();
		entry["Height"] = window->GetHeight();
		entry["Maximized"] = window->IsWindowMaximized();

		nlohmann::json panelClasses = nlohmann::json::array();
		for (const auto& panelWindow : mPanelWindowByClass) {
			if (panelWindow.second != info->WindowID) continue;
			panelClasses.push_back(panelWindow.first.CStr());
		}
		entry["PanelClassNames"] = panelClasses;

		nlohmann::json viewportAssets = nlohmann::json::array();
		for (const TOwningPointer<IEditorViewport>& viewport : mEditorAppContext->EditorViewportManager->GetViewports()) {
			if (viewport->GetWindowIDToRender() != info->WindowID) continue;
			if (!viewport->GetAssetDescriptor()) continue;
			viewportAssets.push_back(static_cast<UInt64>(viewport->GetAssetDescriptor()->Uuid));
		}
		entry["ViewportAssetUUIDs"] = viewportAssets;
		entry["HostedPanelName"] = info->HostedPanel ? info->HostedPanel->GetPanelName().CStr() : "";

		layout.push_back(entry);
	}
	DiskManager::SaveJson(layoutPath.ToString(), layout);
}

void Plu::EditorWindowsManager::RestoreWindows()
{
	if (mWindowsRestored) return;
	mWindowsRestored = true;

	const PathW layoutPath = GetLayoutJSONPath();
	if (layoutPath.ToString().IsEmpty()) return;
	std::optional<nlohmann::json> layout = DiskManager::LoadJson(layoutPath);
	if (!layout.has_value() || !layout->is_array()) return;

	for (const nlohmann::json& entry : layout.value()) {
		const EEditorWindowKind kind = static_cast<EEditorWindowKind>(entry.value("Kind", 0));
		const String title = String(entry.value("Title", std::string("Plu Editor")).c_str());

		UInt32 windowID = 0;
		if (kind != EEditorWindowKind::Main) {
			windowID = CreateEditorWindow(kind, title);
			if (windowID == 0) continue;
			if (TUsePointer<IWindow> window = mApplicationInfo->AppWindowsManager->GetWindow(windowID)) {
				// The window has no platform handle yet (it appears next frame), so geometry goes
				// into its properties and SDLWindow::Init applies it on creation.
				WindowProperties props = window->GetWindowProperties();
				props.Position = IVec2(entry.value("X", -1), entry.value("Y", -1));
				props.Width = entry.value("Width", props.Width);
				props.Height = entry.value("Height", props.Height);
				window->SetWindowProperties(props);
			}
		}

		// Only the mapping — the panels themselves are not opened from here. A panel goes to its
		// saved window at the moment it is opened (EditorPanelManager::InitNewPanels), so opening
		// e.g. the asset browser puts it straight where it belongs, and panels the user had closed
		// stay closed.
		for (const nlohmann::json& panelClassName : entry.value("PanelClassNames", nlohmann::json::array())) {
			mPanelWindowByClass.Insert(String(panelClassName.get<std::string>().c_str()), windowID);
		}

		for (const nlohmann::json& assetUuid : entry.value("ViewportAssetUUIDs", nlohmann::json::array())) {
			mViewportsToRestore.PushBack({assetUuid.get<UInt64>(), windowID});
		}
	}
}

bool Plu::EditorWindowsManager::TryTakePendingPanelWindow(const String &panelClassName, UInt32 &outWindowID)
{
	const UInt32* windowID = mPendingPanelPlacements.Find(panelClassName);
	if (!windowID) return false;
	const UInt32 target = *windowID;
	mPendingPanelPlacements.Remove(panelClassName);
	// The window may have been closed since the layout was written — better the main window than
	// an id nobody builds frames for.
	outWindowID = GetWindowInfo(target) ? target : 0;
	return true;
}

void Plu::EditorWindowsManager::RememberPanelWindow(const String &panelClassName, UInt32 windowID)
{
	if (UInt32* existing = mPanelWindowByClass.Find(panelClassName)) {
		*existing = windowID;
		return;
	}
	mPanelWindowByClass.Insert(panelClassName, windowID);
}

void Plu::EditorWindowsManager::RestoreLayoutAfterProjectOpen()
{
	const PathW newLayoutDir = GetLayoutDirectory();
	if (newLayoutDir.ToString().IsEmpty()) return;

	if (mWindowsRestored) {
		// Same project opened again (a reload) — its layout is already on screen.
		if (mLayoutProjectDir.ToString() == newLayoutDir.ToString()) return;

		// Switching projects switches layouts: write the old one out while its windows still
		// stand, then take them down and start over from the new project's file.
		SaveLayout();
		for (size_t i = mWindows.Size(); i > 0; --i) {
			CloseEditorWindow(mWindows[i - 1]->WindowID);
		}
		// The main window keeps existing, but its docking layout belongs to the other project now.
		if (EditorWindowInfo* mainWindow = GetWindowInfo(0)) {
			mainWindow->IniPathApplied = false;
			mainWindow->IniPath = String();
		}
		mPanelWindowByClass.Clear();
		mPendingPanelPlacements.Clear();
		mViewportsToRestore.Clear();
		mWindowsRestored = false;
	}
	mLayoutProjectDir = newLayoutDir;

	RestoreWindows();

	// Panels: everything the layout knows about. Already-open ones are moved right away, the rest
	// stays pending and is claimed as those panels get opened (TryTakePendingPanelWindow).
	mPendingPanelPlacements = mPanelWindowByClass;
	for (const TOwningPointer<EditorPanel>& panel : mEditorAppContext->EditorPanelManager->GetPanels()) {
		UInt32 target = 0;
		if (!TryTakePendingPanelWindow(panel->GetClass()->TypeName, target)) continue;
		mEditorAppContext->EditorPanelManager->MovePanelToWindow(*panel->GetEngineObjectHandle(), target);
	}

	if (mViewportsToRestore.IsEmpty()) return;

	for (const PendingViewportRestore& pending : mViewportsToRestore) {
		TUsePointer<AssetDescriptor> asset = mApplicationInfo->AppAssetManager->GetAssetDescriptor(pending.AssetUuid);
		// An asset that no longer resolves (renamed, deleted, different project) is simply skipped;
		// its window stays open and empty rather than gaining an orphan viewport.
		if (!asset) continue;
		TUsePointer<IAssetLoader> loader = mApplicationInfo->AppAssetManager->GetAssetLoader(asset->AssetType);
		if (!loader || !loader->GetAssetTypeViewportClass()) continue;

		const PathW assetPath = mApplicationInfo->AppAssetManager->GetAssetPath(pending.AssetUuid).ToString().ToWide();
		mEditorAppContext->EditorViewportManager->CreateViewport(assetPath, loader->GetAssetTypeViewportClass());
		if (TUsePointer<IEditorViewport> viewport = mEditorAppContext->EditorViewportManager->GetViewportForAsset(asset)) {
			mEditorAppContext->EditorViewportManager->MoveViewportToWindow(*viewport->GetEngineObjectHandle(), pending.WindowID);
		}
	}
	mViewportsToRestore.Clear();
}

bool Plu::EditorWindowsManager::TryGetActiveWindowID(UInt32 &outWindowID) const
{
	for (const EditorWindowInfo* info : mWindows) {
		if (info->Kind == EEditorWindowKind::SinglePanel) continue;
		TUsePointer<IWindow> window = mApplicationInfo->AppWindowsManager->GetWindow(info->WindowID);
		if (!window || !window->HasWindowFocus()) continue;
		outWindowID = info->WindowID;
		return true;
	}
	return false;
}

void Plu::EditorWindowsManager::UpdateWindowContents()
{
	for (EditorWindowInfo* info : mWindows) {
		// The main window is the exception on both counts: it is never closed for being empty, and
		// its title belongs to the project (EditorProjectManager::OpenProject).
		if (info->WindowID == 0) continue;
		TUsePointer<IWindow> window = mApplicationInfo->AppWindowsManager->GetWindow(info->WindowID);
		if (!window) continue;

		DynamicArray<String> contents;
		for (const TOwningPointer<EditorPanel>& panel : mEditorAppContext->EditorPanelManager->GetPanels()) {
			if (panel->GetWindowIDToRender() != info->WindowID) continue;
			contents.PushBack(StripImGuiIDFromName(panel->GetPanelName()));
		}
		for (const TOwningPointer<IEditorViewport>& viewport : mEditorAppContext->EditorViewportManager->GetViewports()) {
			if (viewport->GetWindowIDToRender() == info->WindowID) {
				contents.PushBack(StripImGuiIDFromName(viewport->GetWindowTitle()));
			}
			// Panels of a viewport that live in this window without their viewport.
			viewport->CollectPanelTitlesInWindow(info->WindowID, contents);
		}

		if (contents.IsEmpty()) {
			if (info->HadContent) CloseEditorWindow(info->WindowID);
			continue;
		}
		info->HadContent = true;

		const String newTitle = contents.Size() == 1
			? contents[0]
			: String::Format("Group of {0} Windows", contents.Size());
		if (newTitle == info->Title) continue;
		info->Title = newTitle;
		window->SetWindowTitle(newTitle);
	}
}

void Plu::EditorWindowsManager::OnUpdate(float deltaTime)
{
	SyncImGuiIniFiles();
	UpdateWindowContents();

	for (UInt32 windowID : mWindowsToClose) {
		DestroyWindowRecord(windowID);
		mApplicationInfo->AppWindowsManager->RequestCloseWindow(windowID);
	}
	mWindowsToClose.Clear();

	// Safety net for a window that went away without going through CloseEditorWindow (torn down
	// during engine shutdown, or its creation failed).
	for (size_t i = mWindows.Size(); i > 0; --i) {
		EditorWindowInfo* info = mWindows[i - 1];
		if (info->WindowID == 0) continue;
		if (mApplicationInfo->AppWindowsManager->GetWindow(info->WindowID)) continue;
		if (mApplicationInfo->AppWindowsManager->IsWindowClosing(info->WindowID)) continue;
		DestroyWindowRecord(info->WindowID);
	}
}

void Plu::EditorWindowsManager::Shutdown()
{
	mWindowsToClose.Clear();
	for (size_t i = mWindows.Size(); i > 0; --i) {
		const UInt32 windowID = mWindows[i - 1]->WindowID;
		if (windowID == 0) continue;
		DestroyWindowRecord(windowID);
		mApplicationInfo->AppWindowsManager->RequestCloseWindow(windowID);
	}
}
