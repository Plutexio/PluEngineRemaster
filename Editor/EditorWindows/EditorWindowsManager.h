//
// Created by Plutex on 2026-02-20.
//

#ifndef PLUENGINE_EDITORWINDOWSMANAGER_H
#define PLUENGINE_EDITORWINDOWSMANAGER_H
#include <imgui.h>

#include "PluEngine/Core.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "EditorWindowsManager.generated.h"

namespace Plu
{
	class IWindow;
	class IEditorPanel;

	// ImGui window names carry their id behind "##" ("Details##Viewport7"), and everything before
	// "###" is only a label. Neither belongs in an OS title bar, which is what these names end up
	// in once a panel is moved into a window of its own.
	String StripImGuiIDFromName(const String& imguiName);
	struct ApplicationInfo;
	struct EditorAppContext;

	// What an editor window is made of. The kind is fixed when the window is created.
	PLU_ENUM(PyNamespace=Plu)
	enum class EEditorWindowKind : UInt8
	{
		// Window 0: toolbar, menu, window controls and the root dockspace. Hosts anything;
		// closing it ends the application.
		Main,
		// Roughly a copy of the main window: toolbar plus its own root dockspace. Hosts panels
		// and viewports.
		Dockspace,
		// A bare window: a title bar with nothing but the window controls, filled by exactly one
		// panel taken out of a viewport.
		SinglePanel
	};

	// Editor-side record of one OS window. The engine-side window (IWindow, owned by
	// WindowsManager) is addressed by WindowID.
	struct EditorWindowInfo
	{
		UInt32 WindowID = 0;
		EEditorWindowKind Kind = EEditorWindowKind::Main;
		String Title;
		// Root dockspace of this window, refreshed every frame by DrawMainEngineWindow. Used to be
		// the global gDockspaceId, which meant "the window drawn last" — wrong as soon as panels
		// and viewports can live in different windows.
		ImGuiID DockspaceId = 0;
		// True once the window has actually held something. An empty window is closed on sight,
		// but only after it has been filled at least once — a window restored from the layout is
		// legitimately empty until its panels are opened and its viewports resolve (which happens
		// only after a project is open).
		bool HadContent = false;
		// Window class of that dockspace (was the global gWindowClass).
		ImGuiWindowClass WindowClass;
		// SinglePanel only: the panel that fills the window.
		TUsePointer<IEditorPanel> HostedPanel;
		// Where this window's docking layout is loaded from and saved to. Explicitly, by call —
		// never handed to io.IniFilename, see SyncImGuiIniFiles().
		String IniPath;
		bool IniPathApplied = false;
	};

	PLU_CLASS()
	class EditorWindowsManager : public EngineObject
	{
		REFLECTION_BODY_EDITORWINDOWSMANAGER()
	private:
		ApplicationInfo* mApplicationInfo = nullptr;
		EditorAppContext* mEditorAppContext = nullptr;

		// Heap records: ImGui holds `&Info.WindowClass` and panels hold `EditorWindowInfo*` across
		// frames, so growing the array must not move them.
		DynamicArray<EditorWindowInfo*> mWindows;
		// Closing is deferred to OnUpdate: the request usually comes from a menu drawn *inside* the
		// window being closed, i.e. from the middle of the loop that walks mWindows.
		DynamicArray<UInt32> mWindowsToClose;

		// Contents of a saved window that could not be restored yet, kept until RestoreViewports().
		struct PendingViewportRestore
		{
			UInt64 AssetUuid = 0;
			UInt32 WindowID = 0;
		};
		DynamicArray<PendingViewportRestore> mViewportsToRestore;

		// Which window each panel class belongs in, by class name. The layout describes *where* a
		// panel goes, not that it should exist — replaying it at startup would resurrect panels the
		// user had closed.
		HashMap<String, UInt32> mPanelWindowByClass;
		// The subset of the above still waiting to be applied, filled when a project opens. A panel
		// consumes its entry the moment it is opened, and from then on follows the focused window
		// like everything else. Deferred like this because the panels a project opens (the asset
		// browser) are not registered yet at the moment OpenProject returns.
		HashMap<String, UInt32> mPendingPanelPlacements;

		// Loads each window's docking layout once its ImGui context exists, and writes it back
		// whenever ImGui asks (io.WantSaveIniSettings). One .ini per window.
		void SyncImGuiIniFiles();

		void DestroyWindowRecord(UInt32 windowID);
		// Per frame: retitles each secondary window after what it holds and closes the empty ones.
		void UpdateWindowContents();
		// Recreates the saved windows and remembers where their contents belong. Once per session:
		// opening a second project must not spawn a second copy of every window.
		void RestoreWindows();
		bool mWindowsRestored = false;
		// Config/ directory of the project whose layout is currently loaded. Saving goes here, not
		// to "whatever project is open now" — otherwise switching projects would write the previous
		// project's windows into the new project's config.
		PathW mLayoutProjectDir;
	public:
		EditorWindowsManager() = default;
		~EditorWindowsManager() override;

		void Initialize(ApplicationInfo* applicationInfo, EditorAppContext* editorAppContext);

		// Creates the record for window 0, which the app creates and owns itself.
		void RegisterMainWindow(const String& title);

		// Asks the engine for a new OS window and returns its id. The window itself only appears at
		// the top of the next frame (WindowsManager::ProcessPendingWindows), but the id and the
		// record are valid immediately.
		UInt32 CreateEditorWindow(EEditorWindowKind kind, const String& title);
		// Closes a secondary window; whatever it hosted goes back to window 0. Window 0 is ignored.
		// Takes effect at the start of the next frame — safe to call from UI code.
		void CloseEditorWindow(UInt32 windowID);

		// Moves a viewport panel between its parent viewport's window and a SinglePanel window of
		// its own, keeping the hosting records straight on both ends.
		void MoveViewportPanelToWindow(const TUsePointer<IEditorPanel>& panel, UInt32 targetWindowID);

		[[nodiscard]] const DynamicArray<EditorWindowInfo*>& GetWindows() const { return mWindows; }
		[[nodiscard]] EditorWindowInfo* GetWindowInfo(UInt32 windowID) const;

		// The window the user is working in right now — where a freshly opened panel or viewport
		// goes. False when no editor window has focus, or when the focused one is a SinglePanel
		// window (it hosts exactly one panel and takes no newcomers); the caller then falls back
		// to whatever it considers its default.
		[[nodiscard]] bool TryGetActiveWindowID(UInt32& outWindowID) const;

		void OnUpdate(float deltaTime);
		void Shutdown();

		// Layout persistence. The window list (geometry, kind, contents) goes to
		// EditorWindowsLayout.json next to EditorSettings.json; the docking inside each window is
		// ImGui's own business and goes to one .ini per window (see EditorWindowInfo::IniPath).
		//
		// Restoring is split because the two halves become possible at different times: windows and
		// EditorPanels right after startup, viewports only once a project is open and their assets
		// resolve.
		void SaveLayout();
		// Recreates the saved windows and puts their contents back. Runs on project open: that is
		// the first moment viewport assets resolve at all, the panels a project opens itself must
		// land in their saved window rather than in whatever window has focus, and a window
		// restored any earlier would just sit there empty next to the project launcher.
		void RestoreLayoutAfterProjectOpen();

		// Takes the pending placement for a panel class, if the restore left one. Consuming it is
		// the point: the layout places a panel exactly once, afterwards the focused window wins.
		bool TryTakePendingPanelWindow(const String& panelClassName, UInt32& outWindowID);
		// Keeps the mapping current when the user moves a panel, so closing and reopening it lands
		// it back in the same window within the session too.
		void RememberPanelWindow(const String& panelClassName, UInt32 windowID);
	};
}

#endif //PLUENGINE_EDITORWINDOWSMANAGER_H
