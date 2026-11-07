//
// Created by Plutex on 7/6/26.
//

#ifndef PLUENGINE_SKELETONHIERARCHYPANEL_H
#define PLUENGINE_SKELETONHIERARCHYPANEL_H
#include "EditorViewports/IEditorPanel.h"
#include "SkeletonHierarchyPanel.generated.h"
#include "PluEngine/Core.h"
#include "PluSTL/Array/Array.h"
#include "PluSTL/HashMap/HashMap.h"
#include "PluSTL/String/String.h"

namespace Plu
{
	struct SkeletonNode;
	struct Skeleton;

	// Reusable bone/node hierarchy tree. Deliberately viewport-agnostic: it owns its own view
	// state (visibility, node toggle, selection) and resolves the skeleton to draw from the
	// parent viewport's asset descriptor (a Skeleton asset directly, or a SkeletalMesh's
	// embedded skeleton). This lets both SkeletonViewport and SkeletalMeshViewport embed the
	// exact same panel without either one having to know about the other.
	PLU_CLASS()
	class SkeletonHierarchyPanel : public IEditorPanel
	{
		REFLECTION_BODY_SKELETONHIERARCHYPANEL()
	private:
		// Recursively draws one node. Non-bone nodes are skipped (their children rendered
		// in their place) unless ShowNodes is on.
		void DrawNodeTree(SkeletonNode* node);

		// Skeleton the parent viewport is editing: the asset itself when it's a Skeleton,
		// or MeshSkeleton when it's a SkeletalMesh. Null if neither / not loaded yet.
		Skeleton* ResolveSkeleton();

		void DrawContextMenu(SkeletonNode* node);

		// Draws the attach points parented to `node` as leaves under it (or in place of it when
		// the node itself is hidden), each selectable and deletable from its context menu.
		void DrawAttachPointsOf(SkeletonNode* node);

		// Bottom section shown while an attach point is selected: rename plus its parent-relative
		// location/rotation. Every edit here mutates the skeleton asset, so it marks it dirty.
		void DrawAttachPointDetails(Skeleton* skeleton);

		// Attach point names grouped by parent node name. Rebuilt at the top of each OnUpdate and
		// valid only for that frame: the tree walks nodes, while the asset's map is keyed by
		// attach point name, so a per-node lookup has to be derived once per frame.
		HashMap<String, DynamicArray<String>> mAttachPointsByNode;

		// Attach point the tree asked to delete this frame. Applied after the walk so the asset's
		// map is never mutated while it's being iterated/drawn.
		String mAttachPointToDelete;
	public:
		SkeletonHierarchyPanel() = default;
		~SkeletonHierarchyPanel() override = default;

		// Set false to hide the panel (its docked tab is dropped and the space reclaimed).
		// Driven by the host viewport (e.g. SkeletalMeshViewport's "Show Hierarchy" toggle).
		bool Visible = true;

		// Show non-bone structural nodes in the tree in addition to bones.
		bool ShowNodes = false;

		// Name of the node highlighted here and in any peer preview panel (empty = none).
		String SelectedNodeName;

		// Name of the attach point highlighted here and in any peer preview panel (empty = none).
		// Mutually exclusive with SelectedNodeName — selecting either clears the other, so a peer
		// preview always has exactly one gizmo target.
		String SelectedAttachPointName;

		String GetPanelName() override;
		void OnClosed() override;
		void OnOpened() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_SKELETONHIERARCHYPANEL_H
