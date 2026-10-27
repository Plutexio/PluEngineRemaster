//
// Created by Plutex on 7/6/26.
//

#include "SkeletonHierarchyPanel.h"

#include <cstdio>

#include "glm/gtc/type_ptr.hpp"
#include "PluEngine/Application.h"
#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"
#include "PluEngine/AssetTypes/SkeletalMesh/SkeletalMesh.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "UI/IconsFontAwesome7.h"
#include "Utils/AttachPointOverlay.h"
#include "PluEngine/Core/Widgets/DragVector.h"
#include "PluEngine/Core/Widgets/Text.h"

extern Plu::ApplicationInfo* gApplicationInfo;

Plu::String Plu::SkeletonHierarchyPanel::GetPanelName()
{
	return "Hierarchy";
}

void Plu::SkeletonHierarchyPanel::OnClosed()
{
}

void Plu::SkeletonHierarchyPanel::OnOpened()
{
}

Plu::Skeleton* Plu::SkeletonHierarchyPanel::ResolveSkeleton()
{
	TUsePointer<IAssetData> asset =
		gApplicationInfo->AppAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
	if (!asset) return nullptr;

	// Standalone Skeleton asset — use it directly.
	if (Skeleton* skeleton = DynamicCast<Skeleton>(asset).GetRaw())
		return skeleton;

	// SkeletalMesh asset — draw its embedded skeleton.
	if (SkeletalMesh* mesh = DynamicCast<SkeletalMesh>(asset).GetRaw())
		return mesh->MeshSkeleton.GetRaw();

	return nullptr;
}

namespace
{
	// "<node>AttachPoint", then "<node>AttachPoint1", "2", … — first name free in `skeleton`.
	Plu::String MakeUniqueAttachPointName(const Plu::Skeleton& skeleton, const Plu::String& nodeName)
	{
		using namespace Plu;
		const String base = nodeName + "AttachPoint";
		String name = base;
		for (int suffix = 1; skeleton.AttachPoints.Contains(name); ++suffix)
			name = base + String::FromInt(suffix);
		return name;
	}
}

void Plu::SkeletonHierarchyPanel::DrawContextMenu(SkeletonNode* node)
{
	if (ImGui::BeginPopupContextItem()) {
		ImGui::Text("%s",node->NodeName.CStr());
		ImGui::Spacing();
		if (ImGui::Button(ICON_FA_LOCATION_DOT " Add Attach Point")) {
			Skeleton* skeleton = ResolveSkeleton();
			TOwningPointer<SkeletonAttachPoint> newAttachPoint = CreateOwning<SkeletonAttachPoint>();
			newAttachPoint->ParentNodeName = node->NodeName;
			newAttachPoint->AttachPointName = MakeUniqueAttachPointName(*skeleton, node->NodeName);
			PLU_CORE_INFO("New attach point {}", newAttachPoint->AttachPointName.CStr());
			skeleton->AttachPoints.Insert(newAttachPoint->AttachPointName, newAttachPoint);
			// Select the fresh point so its gizmo/transform fields are immediately in reach.
			SelectedAttachPointName = newAttachPoint->AttachPointName;
			SelectedNodeName.Clear();
			MarkSkeletonAssetDirty(gApplicationInfo->AppAssetManager, skeleton);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void Plu::SkeletonHierarchyPanel::DrawAttachPointsOf(SkeletonNode* node)
{
	const DynamicArray<String>* attachPoints = mAttachPointsByNode.Find(node->NodeName);
	if (!attachPoints) return;

	for (const String& attachPointName : *attachPoints)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth
			| ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (attachPointName == SelectedAttachPointName)
			flags |= ImGuiTreeNodeFlags_Selected;

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.25f, 1.0f));
		ImGui::PushID(attachPointName.CStr());
		ImGui::TreeNodeEx("##attach", flags, ICON_FA_LOCATION_DOT " %s", attachPointName.CStr());
		ImGui::PopStyleColor();

		if (ImGui::BeginPopupContextItem()) {
			ImGui::Text("%s", attachPointName.CStr());
			ImGui::Spacing();
			if (ImGui::Button(ICON_FA_TRASH " Delete Attach Point")) {
				mAttachPointToDelete = attachPointName;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (ImGui::IsItemClicked()) {
			SelectedAttachPointName = attachPointName;
			SelectedNodeName.Clear();
		}
		ImGui::PopID();
	}
}

// True when node itself or any descendant would be shown with the current ShowNodes
// setting. Used to decide whether a tree node needs an expand arrow (Leaf otherwise).
// Attach points count as visible children: they're always drawn, whatever ShowNodes says.
static bool AnyVisibleDescendant(Plu::SkeletonNode* node, bool showNodes,
                                 const Plu::HashMap<Plu::String, DynamicArray<Plu::String>>& attachPointsByNode)
{
	if (attachPointsByNode.Contains(node->NodeName)) return true;
	for (const auto& child : node->Children) {
		if (!child) continue;
		bool childIsBone = dynamic_cast<Plu::SkeletonBone*>(child.GetRaw()) != nullptr;
		if (childIsBone || showNodes) return true;
		if (AnyVisibleDescendant(child.GetRaw(), showNodes, attachPointsByNode)) return true;
	}
	return false;
}

void Plu::SkeletonHierarchyPanel::DrawNodeTree(SkeletonNode* node)
{
	if (!node) return;

	const bool isBone = dynamic_cast<SkeletonBone*>(node) != nullptr;
	const bool visible = isBone || ShowNodes;

	if (!visible) {
		// Hidden node: render its attach points and children where it would have been.
		DrawAttachPointsOf(node);
		for (const auto& child : node->Children)
			DrawNodeTree(child.GetRaw());
		return;
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen
		| ImGuiTreeNodeFlags_SpanAvailWidth;
	if (node->NodeName == SelectedNodeName)
		flags |= ImGuiTreeNodeFlags_Selected;
	if (!AnyVisibleDescendant(node, ShowNodes, mAttachPointsByNode))
		flags |= ImGuiTreeNodeFlags_Leaf;

	const char* icon = isBone ? ICON_FA_BONE : ICON_FA_CIRCLE_NODES;
	// Bones bright, structural nodes dimmed so the two read differently at a glance.
	ImGui::PushStyleColor(ImGuiCol_Text, isBone ? ImVec4(0.95f, 0.85f, 0.4f, 1.0f)
	                                            : ImVec4(0.6f, 0.6f, 0.65f, 1.0f));
	ImGui::PushID(node);
	const bool open = ImGui::TreeNodeEx("##node", flags, "%s %s", icon, node->NodeName.CStr());
	ImGui::PopStyleColor();

	DrawContextMenu(node);

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
		SelectedNodeName = node->NodeName;
		SelectedAttachPointName.Clear();
	}

	if (open) {
		DrawAttachPointsOf(node);
		for (const auto& child : node->Children)
			DrawNodeTree(child.GetRaw());
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void Plu::SkeletonHierarchyPanel::DrawAttachPointDetails(Skeleton* skeleton)
{
	TOwningPointer<SkeletonAttachPoint>* found = skeleton->AttachPoints.Find(SelectedAttachPointName);
	if (!found || !*found) {
		// Selection outlived the point it named (deleted, or the viewport switched assets).
		SelectedAttachPointName.Clear();
		return;
	}
	SkeletonAttachPoint* attachPoint = found->GetRaw();

	ImGui::TextDisabled(ICON_FA_LOCATION_DOT " Attach Point");

	// Renaming re-keys the asset's map (the key is the lookup name gameplay code passes to
	// SkeletalMeshComponent::GetAttachPoint*InWorld). Committed on focus loss rather than per
	// keystroke, so half-typed names never collide with an existing point.
	char nameBuffer[128];
	std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", attachPoint->AttachPointName.CStr());
	ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer));
	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		const String newName(nameBuffer);
		if (newName.IsEmpty() || skeleton->AttachPoints.Contains(newName)) {
			PLU_CORE_WARN("Attach point name '{}' is empty or already taken", newName.CStr());
		} else {
			TOwningPointer<SkeletonAttachPoint> attachPointOwner = *found;
			attachPointOwner->AttachPointName = newName;
			skeleton->AttachPoints.Remove(SelectedAttachPointName);
			skeleton->AttachPoints.Insert(newName, attachPointOwner);
			SelectedAttachPointName = newName;
			MarkSkeletonAssetDirty(gApplicationInfo->AppAssetManager, skeleton);
			ImGui::Separator();
			return; // `found` dangles after the re-key; the rest is redrawn next frame.
		}
	}

	ImGui::TextDisabled("Parent: %s", attachPoint->ParentNodeName.CStr());

	// Same transform widget as the scene details panel, on the parent-node-relative transform.
	if (ImGuiWidgets::DragVector("Location", glm::value_ptr(attachPoint->RelativeLocation), 3, 0.1f))
		MarkSkeletonAssetDirty(gApplicationInfo->AppAssetManager, skeleton);
	if (ImGuiWidgets::DragVector("Rotation", glm::value_ptr(attachPoint->RelativeRotation), 3, 0.1f))
		MarkSkeletonAssetDirty(gApplicationInfo->AppAssetManager, skeleton);

	if (ImGui::Button(ICON_FA_TRASH " Delete"))
		mAttachPointToDelete = SelectedAttachPointName;

	ImGui::Separator();
}

void Plu::SkeletonHierarchyPanel::OnUpdate(float deltaTime)
{
	// Hidden: skip submission entirely so the docked tab disappears and its space is reclaimed.
	if (!Visible) return;

	if (BeginPanel())
	{
		Skeleton* skeleton = ResolveSkeleton();

		if (skeleton)
		{
			// Per-frame parent-node → attach points index for the tree walk (see the member's docs).
			mAttachPointsByNode.Clear();
			for (const auto& [name, attachPoint] : skeleton->AttachPoints) {
				if (!attachPoint) continue;
				mAttachPointsByNode[attachPoint->ParentNodeName].PushBack(name);
			}

			// Pinned above the tree: a deep rig scrolls, and the selected attach point's fields
			// must stay reachable while its marker is being dragged in the preview.
			if (!SelectedAttachPointName.IsEmpty())
				DrawAttachPointDetails(skeleton);

			ImGui::TextDisabled("Skeleton: %s", skeleton->SkeletonName.CStr());
			ImGui::SameLine();
			ImGui::TextDisabled("(%llu bones)", (unsigned long long)skeleton->CountBones());
			// Only worth screen space when it isn't the default — but then it explains the size of
			// everything bound to this rig, and it is what later imports get baked at.
			if (skeleton->ImportScale != 1.0f)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(import scale %g)", skeleton->ImportScale);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Uniform scale this rig was imported at. Meshes and animations "
					                  "imported against it are baked at the same scale.");
			}
			ImGui::Separator();

			if (skeleton->RootNode)
			{
				// Deep rigs (e.g. a full human with finger joints) blow past the screen at the
				// default ~21px indent, so tighten it just for this tree.
				ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 10.0f);
				DrawNodeTree(skeleton->RootNode.GetRaw());
				ImGui::PopStyleVar();
			}
			else
				ImGui::TextDisabled("Skeleton has no nodes.");

			// Deferred (see mAttachPointToDelete): the tree/details above are done reading the map.
			if (!mAttachPointToDelete.IsEmpty())
			{
				if (skeleton->AttachPoints.Remove(mAttachPointToDelete)) {
					if (SelectedAttachPointName == mAttachPointToDelete)
						SelectedAttachPointName.Clear();
					MarkSkeletonAssetDirty(gApplicationInfo->AppAssetManager, skeleton);
				}
				mAttachPointToDelete.Clear();
			}
		}
		else
		{
			ImGuiWidgets::EmptyState("No skeleton data", ICON_FA_BONE);
		}
	}
	EndPanel();
}
