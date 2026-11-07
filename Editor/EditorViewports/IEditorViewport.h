#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include "PluEngine/Core/Objects/EngineObject.h"
#include "PluEngine/Render/RenderingInterfaces.h"
#include "PluSTL/PluSTL_FWD.h"
#include "IEditorViewport.generated.h"

namespace Plu
{
    struct AssetDescriptor;
    struct IAssetData;
    struct EditorAppContext;
    class IEditorPanel;
    class IEditorAssetObject;
    class EditorSceneCamera;

    // Where the shared editor camera stood the last time a given viewport had it. The editor owns
    // exactly one EditorSceneCamera; every viewport that navigates in 3D keeps a copy of its own
    // view here and puts it back when the user returns to it, so each viewport behaves as if it
    // had a camera of its own.
    //
    // Deliberately just the view — nothing about what is being rendered. Whether a scene/overlay is
    // loaded and which camera the renderer uses stays SceneManager's business; see the PIE note in
    // Editor/CLAUDE.md for why viewports must not touch that.
    struct EditorViewportCameraState
    {
        Vec3 Location = Vec3(0.0f);
        // Human-readable ("nice") rotation — what SetCameraRotation/GetHumanReadableRotation deal
        // in, not the derived look-at rotation.
        Vec3 Rotation = Vec3(0.0f);
        CameraOptions Options{};
        // Until this viewport has held the camera once there is nothing to restore, and the fields
        // above are meaningless defaults.
        bool Captured = false;
    };
    //READ
    //In editor we have viewports which is for an asset, in the simplest form viewport is an empty dockspace
    //EditorPanels give each viewport functionality, they're "sections" of a viewport
    //Panels are not necessary, we can still have functionality without panels
    PLU_CLASS(Abstract)
    class IEditorViewport : public EngineObject
    {
        REFLECTION_BODY_IEDITORVIEWPORT()
    private:
        TUsePointer<AssetDescriptor> mAsset;
        bool mCanClose = true;
        bool mIsOpen = true;
        bool mCanBeSaved = false;
        HashMap<String, TOwningPointer<IEditorPanel>> mEditorPanels;
        ImGuiWindowClass* windowClass;
        ImGuiID dockID;
        ImVec2 mDockspaceSize = ImVec2(0, 0); //content region available for the internal dockspace, captured each frame before DockSpace()
        DynamicArray<TUsePointer<IEditorPanel>> mPanelsToRegister;
        bool mWasSavedThisFrame = false;
        // Engine window id (IWindow::GetWindowID) this viewport draws into; 0 = the main window.
        UInt32 mWindowIDToRender = 0;
        EditorViewportCameraState mCameraState;

        friend class EditorViewportManager;
        bool mBringToFront = false;

        // Only EditorViewportManager hands the camera over — it is what knows who holds it.
        void SaveCameraState();
        void ApplyCameraState();
    protected:
        EditorAppContext* mEditorAppContext;
        TUsePointer<class EngineObjectManager> mEngineObjectManager;
    public:
        IEditorViewport();
        void Initialize(const TUsePointer<AssetDescriptor> &assetObject);
        virtual ~IEditorViewport() override;
        void Shutdown();

        TUsePointer<AssetDescriptor> GetAssetDescriptor();
        virtual String GetWindowTitle(); //Imgui Window Title, with formating ID
        virtual String GetWindowName(); //Asset Name
        virtual String GetDockspaceName();

        void SetCanClose(bool canClose);
        [[nodiscard]] bool IsOpen() const;
        [[nodiscard]] bool IsCanBeSaved() const;
        bool WasSavedThisFrame() const;
        void MarkThisFrameAsSaved();
        void ViewportChangedAsset() const;

        TUsePointer<IEditorPanel> AddPanel(TypeInfo* classToCreate, bool canBeClosed);

        // Override to true in viewports that navigate the shared editor camera. Viewports that
        // never touch it (texture, shader, material…) leave it alone so they can't drag it around
        // just by being focused.
        virtual bool UsesEditorCamera() const { return false; }
        // The one and only editor camera. Whatever it currently holds belongs to whichever
        // viewport last claimed it.
        [[nodiscard]] EditorSceneCamera* GetEditorCamera() const;
        // Makes this viewport the camera's owner: the previous owner's view is stashed and this
        // viewport's is restored. No-op if it already owns it or doesn't use the camera at all.
        void ClaimEditorCamera();

        template<class T>
        T* GetPanelSlow(); //Get panel by type. SLOW!!!! Only use once when setting up docking

        virtual void OnOpened() = 0;
        virtual void OnClosed() = 0;
        virtual void OnPanelRegister() = 0;
        virtual void OnUpdate(float deltaTime) = 0;
        virtual void OnInit() {}
        
        [[nodiscard]] ImGuiWindowClass* GetViewportWindowClass() const;

        [[nodiscard]] UInt32 GetWindowIDToRender() const { return mWindowIDToRender; }
        void SetWindowIDToRender(UInt32 windowID) { mWindowIDToRender = windowID; }
        // Re-targets every panel that was still drawing in `fromWindowID` (i.e. following the
        // viewport rather than living in its own SinglePanel window) to `toWindowID`.
        void MovePanelsFollowingViewport(UInt32 fromWindowID, UInt32 toWindowID);
        // Appends the (ImGui-id-stripped) titles of this viewport's panels currently drawn in
        // windowID — i.e. the ones pulled out of the viewport into a window of their own.
        void CollectPanelTitlesInWindow(UInt32 windowID, DynamicArray<String>& outTitles) const;
    protected:
        void UpdatePanels(float deltaTime);
        bool BeginWindow();
        void EndWindow();
        [[nodiscard]] ImGuiID GetWindowDockID() const;
        [[nodiscard]] ImVec2 GetDockspaceSize() const; //valid size to hand DockBuilderSetNodeSize when building a layout
        void SetCanBeSaved(bool canBeSaved);
    };

    template <class T>
    T* IEditorViewport::GetPanelSlow()
    {
        for (const auto& panel : mEditorPanels)
        {
            if (T* result = dynamic_cast<T*>(panel.second.GetRaw()))
            {
                return result;
            }
        }
        return nullptr;
    }
}
