#include "IEditorViewport.h"

#include <imgui_internal.h>

#include "EditorAppContext.h"
#include "EditorWindows/EditorWindowMoveMenu.h"
#include "EditorWindows/EditorWindowsManager.h"
#include "EditorViewportManager.h"
#include "IEditorPanel.h"
#include "Managers/Scene/EditorCamera.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"

extern Plu::TUsePointer<Plu::EngineObjectManager> gEngineObjectManager;
extern Plu::EditorAppContext* gEditorAppContext;

Plu::IEditorViewport::IEditorViewport()
{
    windowClass = new ImGuiWindowClass();
    mEditorAppContext = gEditorAppContext;
    mEngineObjectManager = gEngineObjectManager;
}

void Plu::IEditorViewport::Initialize(const TUsePointer<AssetDescriptor> &assetObject)
{
    mAsset = assetObject;
    mCanBeSaved = assetObject->LoaderType == AssetLoaderType::JSON;
    OnInit();
}

Plu::IEditorViewport::~IEditorViewport()
{
    delete windowClass;
}

void Plu::IEditorViewport::Shutdown()
{
    for (const std::pair<String, TOwningPointer<IEditorPanel>> panel : mEditorPanels)
    {
        panel.second->OnClosed();
        gEngineObjectManager->DestroyObject(*panel.second->GetEngineObjectHandle());
    }
    mEditorPanels.Clear();
}

Plu::TUsePointer<Plu::AssetDescriptor> Plu::IEditorViewport::GetAssetDescriptor()
{
    return mAsset;
}

Plu::String Plu::IEditorViewport::GetWindowTitle()
{
    return GetWindowName();
}

Plu::String Plu::IEditorViewport::GetWindowName()
{
    return mAsset ? mAsset->AssetName : "NOASSET";
}

Plu::String Plu::IEditorViewport::GetDockspaceName()
{
    return GetWindowName() + "Dockspace";
}

void Plu::IEditorViewport::SetCanClose(bool canClose)
{
    mCanClose = canClose;
}

bool Plu::IEditorViewport::IsOpen() const
{
    return mIsOpen;
}

bool Plu::IEditorViewport::IsCanBeSaved() const
{
    return mCanBeSaved;
}

bool Plu::IEditorViewport::WasSavedThisFrame() const
{
    return mWasSavedThisFrame;
}

void Plu::IEditorViewport::MarkThisFrameAsSaved()
{
    mWasSavedThisFrame = true;
}

void Plu::IEditorViewport::ViewportChangedAsset() const
{
    gEditorAppContext->EditorAssetManager->MarkAssetDirty(mAsset);
}

Plu::TUsePointer<Plu::IEditorPanel> Plu::IEditorViewport::AddPanel(TypeInfo *classToCreate, bool canBeClosed)
{
    TUsePointer<IEditorPanel> newPanel = DynamicCast<IEditorPanel>(gEngineObjectManager->CreateObject(classToCreate));
    mPanelsToRegister.PushBack(newPanel);
    newPanel->Initialize(gEngineObjectManager->GetObjectAsUser<IEditorViewport>(*this->GetEngineObjectHandle()), false);
    return newPanel;
}

ImGuiWindowClass* Plu::IEditorViewport::GetViewportWindowClass() const
{
    return windowClass;
}

Plu::EditorSceneCamera* Plu::IEditorViewport::GetEditorCamera() const
{
    return gEditorAppContext->EditorSceneCamera.IsValid() ? gEditorAppContext->EditorSceneCamera.GetRaw() : nullptr;
}

void Plu::IEditorViewport::ClaimEditorCamera()
{
    if (!UsesEditorCamera()) return;
    gEditorAppContext->EditorViewportManager->SetCameraOwner(
        gEngineObjectManager->GetObjectAsUser<IEditorViewport>(*GetEngineObjectHandle()));
}

void Plu::IEditorViewport::SaveCameraState()
{
    EditorSceneCamera* camera = GetEditorCamera();
    if (!camera) return;
    mCameraState.Location = camera->GetCameraLocation();
    mCameraState.Rotation = camera->GetHumanReadableRotation();
    mCameraState.Options = *camera->GetCameraOptions();
    mCameraState.Captured = true;
}

void Plu::IEditorViewport::ApplyCameraState()
{
    EditorSceneCamera* camera = GetEditorCamera();
    if (!camera) return;

    // First claim: this viewport has no view of its own yet, so it adopts whatever the camera looks
    // like right now instead of slamming it to the origin. That's also what makes the camera
    // position a scene load restored ("EditorCameraLocationLoaded"), or a panel's initial framing,
    // become the viewport's starting view.
    if (!mCameraState.Captured)
    {
        SaveCameraState();
        return;
    }

    // Location first: SetCameraRotation derives the look-at from an orbit point around the current
    // location, so a stale location here would bend the restored rotation.
    camera->SetCameraOptions(&mCameraState.Options);
    camera->SetCameraLocation(mCameraState.Location);
    camera->SetCameraRotation(mCameraState.Rotation);
}

void Plu::IEditorViewport::MovePanelsFollowingViewport(UInt32 fromWindowID, UInt32 toWindowID)
{
    for (std::pair<String, TOwningPointer<IEditorPanel>> panel : mEditorPanels)
    {
        if (panel.second->GetWindowIDToRender() != fromWindowID) continue;
        panel.second->SetWindowIDToRender(toWindowID);
    }
}

void Plu::IEditorViewport::CollectPanelTitlesInWindow(UInt32 windowID, DynamicArray<String>& outTitles) const
{
    if (windowID == mWindowIDToRender) return;
    for (const std::pair<String, TOwningPointer<IEditorPanel>>& panel : mEditorPanels)
    {
        if (panel.second->GetWindowIDToRender() != windowID) continue;
        outTitles.PushBack(StripImGuiIDFromName(panel.second->GetPanelTitle()));
    }
}

void Plu::IEditorViewport::UpdatePanels(float deltaTime)
{
    for (std::pair<String, TOwningPointer<IEditorPanel>> panel : mEditorPanels)
    {
        // A panel moved out into its own SinglePanel window is drawn by that window instead —
        // drawing it here as well would submit the same ImGui window twice in one frame.
        if (panel.second->GetWindowIDToRender() != mWindowIDToRender) continue;
        panel.second->OnUpdate(deltaTime);
    }
}

bool Plu::IEditorViewport::BeginWindow()
{
    if (!ImGui::GetWindowDockNode()) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.4f, 0.4f, 0.4f, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    }
    ImGui::SetNextWindowClass(gEditorAppContext->EditorViewportManager->GetViewportWindowClass());
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (mEditorAppContext->EditorAssetManager->IsAssetDirty(mAsset)) {
        flags |= ImGuiWindowFlags_UnsavedDocument;
    }
    static HashMap<String, bool> lastWindowState;

    if (mBringToFront)
    {
        ImGui::SetNextWindowFocus();
        mBringToFront = false;
    }

    bool open = ImGui::Begin(GetWindowTitle().CStr(), mCanClose ? &mIsOpen : nullptr, flags);
    // Right-click on the viewport's dock tab.
    if (ImGui::BeginPopupContextItem()) {
        DrawMoveToWindowMenu(mWindowIDToRender, EEditorWindowKind::Dockspace, GetWindowTitle(),
            [this](UInt32 targetWindowID) {
                gEditorAppContext->EditorViewportManager->MoveViewportToWindow(
                    *this->GetEngineObjectHandle(), targetWindowID);
            });
        ImGui::EndPopup();
    }
    if (open != lastWindowState[GetWindowTitle()]) {
        if (open) {
            for (auto panel : mEditorPanels) {
                panel.second->OnOpened();
            }
            // Coming back into view (tab switch, window re-shown) takes the camera back, so the
            // viewport renders framed as the user left it without waiting for a hover. After the
            // panels' OnOpened, which is where a viewport re-creates whatever it previews.
            ClaimEditorCamera();
        } else {
            for (auto panel : mEditorPanels) {
                panel.second->OnClosed();
            }
        }
    }
    mWasSavedThisFrame = false;
    if (open)
    {
        // Nobody holds the camera (the owner viewport was just closed) — take it rather than
        // leave the view stuck wherever that viewport left it.
        if (!gEditorAppContext->EditorViewportManager->GetCameraOwner().IsValid()) ClaimEditorCamera();

        windowClass->ClassId = ImGui::GetID(GetDockspaceName().CStr());
        dockID = ImGui::GetID(GetDockspaceName().CStr());
        // Capture the region the dockspace will fill BEFORE submitting it (DockSpace() consumes it).
        // OnPanelRegister() builds the layout off this — on the first frame a window appears its size
        // hasn't settled yet, and a too-small height makes nested Y-splits collapse to a corner.
        mDockspaceSize = ImGui::GetContentRegionAvail();
        ImGui::DockSpace(dockID,ImVec2(0,0),0,windowClass);

        //Now register all unregistered panels. Defer until the dockspace has a sane size so
        //DockBuilder split ratios are computed against the real region, not a first-frame stub.
        if (!mPanelsToRegister.IsEmpty() && mDockspaceSize.x > 50.0f && mDockspaceSize.y > 50.0f)
        {
            for (const auto& panel : mPanelsToRegister)
            {
                // A panel belongs to its viewport's window until the user pulls it out. Without
                // this it would keep the default 0 and a viewport living in a secondary window
                // would draw no panels at all — UpdatePanels only draws the ones whose window
                // matches the viewport's.
                panel->SetWindowIDToRender(mWindowIDToRender);
                mEditorPanels.Insert(panel->GetPanelName(), gEngineObjectManager->GetObjectAsOwner<IEditorPanel>(*panel->GetEngineObjectHandle()));
                //ImGui::DockBuilderDockWindow(panel->GetPanelTitle().c_str(), dockID);
                //ImGui::DockBuilderFinish(dockID);
                panel->OnOpened();
            }
            mPanelsToRegister.Clear();
            OnPanelRegister();
        }


        if (mCanBeSaved) {
            if (ImGui::Shortcut(ImGuiMod_Ctrl + ImGuiKey_S) && !WasSavedThisFrame()) {
                MarkThisFrameAsSaved();
                mEditorAppContext->EditorAssetManager->SaveAsset(mAsset);
            }
        }
    }
    lastWindowState[GetWindowTitle()] = open;
    if (!mIsOpen) {
        gEditorAppContext->EditorViewportManager->CloseViewport(*this->GetEngineObjectHandle());
    }
    return open;
}

void Plu::IEditorViewport::EndWindow()
{
    ImGui::End();
    if (!ImGui::GetWindowDockNode()) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }    
}

ImGuiID Plu::IEditorViewport::GetWindowDockID() const
{
    return dockID;
}

ImVec2 Plu::IEditorViewport::GetDockspaceSize() const
{
    return mDockspaceSize;
}

void Plu::IEditorViewport::SetCanBeSaved(bool canBeSaved)
{
    mCanBeSaved = canBeSaved;
}
