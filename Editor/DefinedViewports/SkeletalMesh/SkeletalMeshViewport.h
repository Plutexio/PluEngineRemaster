//
// Created by Plutex on 7/7/26.
//

#ifndef PLUENGINE_SKELETALMESHVIEWPORT_H
#define PLUENGINE_SKELETALMESHVIEWPORT_H
#include "EditorViewports/IEditorViewport.h"
#include "SkeletalMeshViewport.generated.h"
#include "PluEngine/Gameplay/Components/SkeletalMeshComponent.h"
#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/Gameplay/Components/StaticMeshComponent.h"
#include "Utils/GizmoUtils.h"

namespace Plu
{
	// GameObject spawned into the viewport's overlay scene to host the previewed skeletal mesh.
	// Mirrors StaticMeshViewport's EditorMeshObject: one SkeletalMeshComponent that the panels
	// drive (mesh/material/animation), rendered through the normal snapshot path.
	PLU_CLASS()
	class EditorSkeletalMeshObject : public GameObject
	{
		REFLECTION_BODY_EDITORSKELETALMESHOBJECT()
	public:
		EditorSkeletalMeshObject() = default;
		virtual ~EditorSkeletalMeshObject() override = default;

		void OnSetupComponents() override;

		TUsePointer<SkeletalMeshComponent> MeshComponent;
	};

	// Host for one attach point preview mesh in the overlay scene. Structurally identical to
	// StaticMeshViewport's EditorMeshObject, but deliberately its own class: the overlay scene is a
	// singleton shared with StaticMeshViewport, and that viewport finds its mesh with
	// GetGameObjectOfClass(EditorMeshObject) — reusing that class would let it grab a preview.
	PLU_CLASS()
	class EditorAttachPointPreviewObject : public GameObject
	{
		REFLECTION_BODY_EDITORATTACHPOINTPREVIEWOBJECT()
	public:
		EditorAttachPointPreviewObject() = default;
		virtual ~EditorAttachPointPreviewObject() override = default;

		void OnSetupComponents() override;

		TUsePointer<StaticMeshComponent> MeshComponent;
	};

	struct MaterialInfo;
	struct StaticMesh;

	// A static mesh pinned to one skeleton attach point so the user can eyeball how a prop (sword,
	// hat, …) sits on the rig while it animates. Pure view state: attach point preview meshes are
	// NOT part of the Skeleton asset, so nothing here ever marks anything dirty and the whole thing
	// is forgotten when the viewport closes.
	struct AttachPointPreview
	{
		// Mesh to show; null means "no preview for this attach point" (the entry is kept so the
		// scale the user dialled in survives clearing and re-picking a mesh).
		TUsePointer<StaticMesh> Mesh;

		// Extra scale on top of the mesh's authored size — props are rarely modelled at rig scale.
		// The attach point itself has no scale, so this lives here rather than on the asset.
		Vec3 Scale{1.0f};

		// Object carrying the mesh in the overlay scene. Weak on purpose: the overlay scene is
		// recreated every time the viewport panel reopens, which drops these objects — the per-frame
		// sync in SkeletalMeshViewportPanel notices the dead pointer and respawns.
		TUsePointer<EditorAttachPointPreviewObject> Object;
	};

	// Viewport for standalone SkeletalMesh assets. Renders the mesh through the overlay scene
	// (same FBO path as StaticMeshViewport) and adds skeletal-only preview state: a bone overlay
	// toggle and an animation picker (playback loops, no timeline / scrubbing).
	PLU_CLASS()
	class SkeletalMeshViewport : public IEditorViewport
	{
		REFLECTION_BODY_SKELETALMESHVIEWPORT()
	public:
		SkeletalMeshViewport() = default;
		virtual ~SkeletalMeshViewport() override = default;

		// --- Shared preview state (never marks the asset dirty — pure view settings) ---
		// Preview material applied to the mesh; not part of the asset.
		TUsePointer<MaterialInfo> Material;

		// Preview meshes hung off attach points, keyed by attach point name. Entries are created by
		// the details panel and dropped when the named attach point disappears from the skeleton.
		HashMap<String, AttachPointPreview> AttachPointPreviews;

		// Material the preview meshes render with. Separate from `Material` on purpose: that one is
		// a *skeletal* material (its vertex shader reads the bone SSBO) and would draw a static mesh
		// wrong, so previews get the static-mesh viewport material instead.
		TUsePointer<MaterialInfo> AttachPointPreviewMaterial;

		// Draw the animated bone skeleton as an ImGui overlay on top of the rendered mesh.
		bool ShowBones = false;

		// Expand the reusable bone/node hierarchy tree (same panel as SkeletonViewport) docked on
		// the left. Off by default; the details panel mirrors this into the panel's Visible flag.
		bool ShowHierarchy = false;

		// Set true to make the viewport panel fit the shared editor camera to the mesh bounds on
		// its next update (on open, or when the user clicks "Frame"). Stays pending until the mesh
		// has loaded vertices to measure, so async loads still get framed.
		bool NeedsFraming = true;

		// Bone-posing gizmo mode (view state; the viewport's overlay controls drive these).
		GizmoOperation GizmoOp = GizmoOperation::TRANSLATE;
		GizmoOperationSpace GizmoSpace = GizmoOperationSpace::WORLD;

		bool UsesEditorCamera() const override { return true; }

		void OnOpened() override;
		void OnClosed() override;
		void OnPanelRegister() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_SKELETALMESHVIEWPORT_H
