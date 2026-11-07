//
// Created by Plutex on 31.12.2025.
//

#ifndef PLUENGINE_EDITORAPP_H
#define PLUENGINE_EDITORAPP_H
#include "imgui.h"
#include "PluEngine/Application.h"

namespace Plu
{
    class EditorAssetImporter;
    struct EditorAppContext;
    class EditorProjectManager;
    class EditorPanelManager;

    class PluEditor final : public Application
    {
        EditorAppContext* mEditorAppContext;
        TOwningPointer<EditorPanelManager> mPanelManager;
        TOwningPointer<EditorProjectManager> mEditorProjectManager;
        // True while a font-atlas rebuild (e.g. font-size change) is still settling and the ImGui
        // handoff must stay in lockstep with the render thread. See OnTick() and
        // RenderingManager::BeginImGuiLockstep().
        bool mImGuiAtlasSettling = false;
        // Seconds since the shader/script watchers were last polled. Time based, not a frame
        // counter: under power saving frames stop coming, the watchers must not.
        // Hash of the ImGui draw data last handed to the render thread. A probe frame (power
        // saving, pointer moved) is published only if its hash differs.
        UInt64 mPublishedImGuiHash = 0;
        float mFileWatchTimer = 0.0f;
        void PollFileWatchers(float deltaTime);
        //This for passa on GH
        friend inline float DrawToolbarWindow(float toolbarHeight, int windowID);

        bool mAssetSaveConfirmShow = false;
        bool mAssetSaveConfirm = false;

        bool mIsDropOnWindow = false;
        TUsePointer<EditorAssetImporter> mAssetImporter;
        // Raised by ClearAfterImport(); the importer is destroyed once its RenderUI() returned.
        bool mAssetImportFinished = false;
        DynamicArray<Path> mPathsToImport;
        void ClearAfterImport();
    public:
        PluEditor();
        virtual ~PluEditor() override;

        bool OnInit() override;
        void OnPostInit() override;
        void OnShutdown() override;
        void OnTick(float deltaTime) override;
        void OnIdleTick(float deltaTime) override;
        bool WantsContinuousFrames() override;

        void OnRequestedGameExit() override;

        void OnRequestedWindowClose(TUsePointer<IWindow> window) override;

        // Set once the layout has been saved from a close request, where the whole window list is
        // still intact. Stops OnShutdown from overwriting that with a poorer picture.
        bool mLayoutSavedOnQuit = false;

    private:
        // Builds the whole editor UI. Driven by the editor itself from OnTick(), bracketed by
        // ImGui::NewFrame()/Render(), and the resulting draw data is handed to the render
        // thread via RenderingManager::SubmitImGuiDrawData().
        void OnImGuiRender();
        // Builds one window's UI, branching on what kind of editor window it is.
        void OnImGuiRenderForWindow(struct EditorWindowInfo& windowInfo);
        void DrawSinglePanelWindow(struct EditorWindowInfo& windowInfo);
        void DrawNewProjectPopup();
    };
}

#endif //PLUENGINE_EDITORAPP_H