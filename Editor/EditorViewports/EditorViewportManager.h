#pragma once

#include <filesystem>


#include <imgui.h>

#include "IEditorViewport.h"
#include "PluSTL/PluSTL_FWD.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "EditorViewportManager.generated.h"

namespace Plu
{
    class IEditorPanel;
    class EngineObjectManager;
    //Viewports are newer and better solution but not all cases require them.
    //So general thought is that viewports are for assets and panels for everything else e.g. AssetBrowser, Theme Editor
    PLU_CLASS()
    class EditorViewportManager : public EngineObject 
    {
        REFLECTION_BODY_EDITORVIEWPORTMANAGER()
    private:
        DynamicArray<TOwningPointer<IEditorViewport>> mViewports; //Viewport is for a given asset

        ImGuiWindowClass* mWindowClass;
        // A viewport waiting to be docked, together with the window whose dockspace it belongs in.
        struct PendingViewportDock
        {
            String WindowTitle;
            UInt32 WindowID = 0;
        };
        DynamicArray<PendingViewportDock> mWindowsToDock;

        TUsePointer<IEditorPanel> mHoveredPanel;

        // The editor has a single EditorSceneCamera, and this is the viewport currently driving it.
        // Handing it over stashes the outgoing viewport's view and restores the incoming one's, so
        // each viewport looks like it kept a camera of its own.
        TUsePointer<IEditorViewport> mCameraOwner;

        DynamicArray<TUsePointer<IEditorViewport>> mViewportsToAnnihilateFromExistanceInOurWorld;
    public:
        EditorViewportManager();
        ~EditorViewportManager() override;

        TUsePointer<IEditorPanel> GetHoveredPanel();

        void SetCameraOwner(const TUsePointer<IEditorViewport>& viewport);
        TUsePointer<IEditorViewport> GetCameraOwner() const;

        void CloseViewport(EngineObjectHandle viewport);

        void CreateViewport(const PathW& assetPath, const TypeInfo* classOfViewport);
        TUsePointer<IEditorViewport> GetViewport(TypeInfo* viewportClass);
        TUsePointer<IEditorViewport> GetViewportForAsset(TUsePointer<AssetDescriptor> asset);

        bool AreThereViewportsToDock() const;

        void Tick(float deltaTime, UInt32 windowID);
        void DockNewViewports(UInt32 windowID);
        // Sends every viewport that was rendering into windowID back to the main window; called
        // when that window closes so its viewports reappear instead of vanishing.
        void ReturnViewportsFromWindow(UInt32 windowID);
        // Sends every viewport *panel* that was pulled out into windowID back into its parent
        // viewport. Separate from ReturnViewportsFromWindow: a panel in a SinglePanel window has
        // left its viewport behind, so the viewport itself is not in that window at all.
        void ReturnViewportPanelsFromWindow(UInt32 windowID);
        [[nodiscard]] const DynamicArray<TOwningPointer<IEditorViewport>>& GetViewports() const { return mViewports; }
        // Moves a viewport (with the panels that still live inside it) to another editor window.
        void MoveViewportToWindow(EngineObjectHandle viewport, UInt32 targetWindowID);
        void Shutdown();

        ImGuiWindowClass* GetViewportWindowClass() const;
    };
}
