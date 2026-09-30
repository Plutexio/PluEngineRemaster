//
// Created by Plutex on 1/14/26.
//

#include "SceneObjectDetailsPanel.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"

#include "EditorAppContext.h"
#include "glm/gtc/type_ptr.hpp"
#include "PluEngine/AssetTypes/SkeletalMesh/SkeletalMesh.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"
#include "PluEngine/Gameplay/Components/SkeletalMeshComponent.h"
#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/Gameplay/GameObjectComponent.h"
#include "PluEngine/Gameplay/WorldComponent.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Gameplay/Scenes/SceneManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"
#include "UI/IconsFontAwesome7.h"
#include "SceneStructurePanel.h"
#include "PluEngine/Gameplay/Components/ParticleSpawnerComponent.h"
#include "Utils/RGBTransformDragger.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Core/Widgets/TypeTree.h"

extern Plu::EditorAppContext* gEditorAppContext;
extern Plu::TUsePointer<Plu::EngineObjectManager> gEngineObjectManager;

Plu::String Plu::SceneInspectorPanel::GetPanelName()
{
	return "Inspector";
}

void Plu::SceneInspectorPanel::OnClosed()
{
}

void Plu::SceneInspectorPanel::OnOpened()
{
}

void GatherParents(Plu::TypeInfo* typeInfo, DynamicArray<Plu::TypeInfo*>* parents)
{
	if (!typeInfo) return;
	parents->PushBack(typeInfo);
	GatherParents(typeInfo->BaseType, parents);
}

static Plu::TUsePointer<Plu::WorldComponent> GetDraggedWorldComponent(const ImGuiPayload* payload)
{
	if (!payload || payload->DataSize != sizeof(Plu::EngineObjectHandle)) return nullptr;
	const Plu::EngineObjectHandle handle = *static_cast<const Plu::EngineObjectHandle*>(payload->Data);
	if (!gEngineObjectManager->IsValid(handle)) return nullptr;
	return gEngineObjectManager->GetObjectAsUser<Plu::WorldComponent>(handle);
}

static Plu::TUsePointer<Plu::GameObject> GetDraggedGameObject(const ImGuiPayload* payload)
{
	if (!payload || payload->DataSize != sizeof(Plu::EngineObjectHandle)) return nullptr;
	const Plu::EngineObjectHandle handle = *static_cast<const Plu::EngineObjectHandle*>(payload->Data);
	if (!gEngineObjectManager->IsValid(handle)) return nullptr;
	return gEngineObjectManager->GetObjectAsUser<Plu::GameObject>(handle);
}

// Re-nests the Structure panel: its row order comes from the attachment graph, and it only rebuilds
// on this event (see Editor/CLAUDE.md).
static void NotifyAttachmentChanged()
{
	if (Plu::TUsePointer<Plu::SceneWorld> world = gEditorAppContext->EditorScenesManager->GetCurrentWorld()) {
		world->GetObjectEventDispatcher()->Dispatch("GameObjectsChanged", nullptr);
	}
}

// Drop target on one row of the component tree. Takes two kinds of payload:
//
//   * a WorldComponent dragged inside this tree -> reparent the component (WorldComponent::AttachTo);
//   * a GameObject dragged out of the Structure panel -> attach that whole object to this component
//     (GameObject::AttachToComponent). This is the "rifle on the character's camera" gesture: drop
//     the rifle on the CameraComponent row and it rides the camera, aim included.
//
// Payloads are only accepted for drops the engine would honour, so an illegal target (itself, one of
// its own descendants, its current parent) never even highlights. KeepWorld throughout: a hierarchy
// edit must not move anything in the scene.
static bool WorldComponentAttachTarget(const Plu::TUsePointer<Plu::GameObject>& owner, Plu::WorldComponent* newAttachPoint)
{
	using namespace Plu;
	bool attached = false;
	if (ImGui::BeginDragDropTarget()) {
		const ImGuiPayload* peek = ImGui::GetDragDropPayload();
		if (peek && peek->IsDataType(CSceneComponentDragPayload)) {
			TUsePointer<WorldComponent> dragged = GetDraggedWorldComponent(peek);
			const bool legal = dragged
				&& dragged.GetRaw() != newAttachPoint
				&& dragged->GetParentComponent().GetRaw() != newAttachPoint
				&& dragged->GetParentGameObject().GetRaw() == owner.GetRaw()
				&& (!newAttachPoint || (newAttachPoint->GetParentGameObject().GetRaw() == owner.GetRaw()
					&& !newAttachPoint->IsAttachedTo(dragged.GetRaw())));
			if (legal && ImGui::AcceptDragDropPayload(CSceneComponentDragPayload)) {
				dragged->AttachTo(newAttachPoint, EAttachmentRule::KeepWorld);
				attached = true;
			}
		} else if (peek && peek->IsDataType(CSceneObjectDragPayload)) {
			TUsePointer<GameObject> dragged = GetDraggedGameObject(peek);
			// An object cannot ride its own components, nor a component of anything already riding it.
			const bool legal = dragged
				&& dragged.GetRaw() != owner.GetRaw()
				&& !owner->IsAttachedToObject(dragged.GetRaw())
				&& (newAttachPoint ? dragged->GetAttachParentComponent().GetRaw() != newAttachPoint
				                   : dragged->GetAttachParentObject().GetRaw() != owner.GetRaw());
			if (legal && ImGui::AcceptDragDropPayload(CSceneObjectDragPayload)) {
				if (newAttachPoint) {
					dragged->AttachToComponent(newAttachPoint, "", EAttachmentRule::KeepWorld);
				} else {
					dragged->AttachToObject(owner.GetRaw(), EAttachmentRule::KeepWorld);
				}
				NotifyAttachmentChanged();
				attached = true;
			}
		}
		ImGui::EndDragDropTarget();
	}
	return attached;
}

// Attachment of the whole object (GameObject::AttachToComponent / AttachToObject). Attaching is
// done by dragging rows in the Structure panel; this is where the attachment is refined — which of
// the parent's components (or the parent object itself) is ridden, and which socket on it — and
// where it can be released. Returns true when something changed.
bool ObjectAttachmentSection(const Plu::TUsePointer<Plu::GameObject>& object)
{
	using namespace Plu;
	TUsePointer<GameObject> parent = object->GetAttachParentObject();
	if (!parent) return false;

	bool changed = false;
	ImGui::SeparatorText("Attachment");
	ImGui::Text(ICON_FA_LINK " %s", parent->GetObjectName().CStr());

	// Attach point inside the parent: its own transform, or any of its world components.
	const DynamicArray<TUsePointer<WorldComponent>>* parentComponents = parent->GetObjectWorldComponents();
	TUsePointer<WorldComponent> currentComponent = object->GetAttachParentComponent();
	const char* currentLabel = currentComponent ? currentComponent->GetComponentName().CStr() : "(object transform)";
	if (ImGui::BeginCombo("Attach to", currentLabel)) {
		if (ImGui::Selectable("(object transform)", !currentComponent)) {
			object->AttachToObject(parent.GetRaw(), EAttachmentRule::KeepWorld);
			changed = true;
		}
		for (const auto& comp : *parentComponents) {
			if (!comp) continue;
			const bool selected = currentComponent.GetRaw() == comp.GetRaw();
			if (ImGui::Selectable(comp->GetComponentName().CStr(), selected)) {
				object->AttachToComponent(comp.GetRaw(), "", EAttachmentRule::KeepWorld);
				changed = true;
			}
		}
		ImGui::EndCombo();
	}

	// Sockets exist only on skeletal meshes — the attach points authored on their skeleton.
	if (currentComponent && currentComponent->GetClass()->IsDerivedOfOrSame(SkeletalMeshComponent::GetStaticClass())) {
		auto* skeletalMesh = static_cast<SkeletalMeshComponent*>(currentComponent.GetRaw());
		TUsePointer<SkeletalMesh> mesh = skeletalMesh->GetSkeletalMesh();
		if (mesh && mesh->MeshSkeleton) {
			const String& socket = object->GetAttachSocketName();
			if (ImGui::BeginCombo("Socket", socket.IsEmpty() ? "(none)" : socket.CStr())) {
				if (ImGui::Selectable("(none)", socket.IsEmpty())) {
					object->AttachToComponent(currentComponent.GetRaw(), "", EAttachmentRule::KeepWorld);
					changed = true;
				}
				for (const auto& [attachName, attachPoint] : mesh->MeshSkeleton->AttachPoints) {
					if (ImGui::Selectable(attachName.CStr(), socket == attachName)) {
						// SnapToTarget: picking a socket in the inspector means "sit on that bone",
						// which is what the old AttachToSkeletalMeshComponent did. Nudge it off the
						// bone afterwards with the relative transform if an offset is wanted.
						object->AttachToComponent(currentComponent.GetRaw(), attachName, EAttachmentRule::SnapToTarget);
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
		}
	}

	if (ImGui::Button(ICON_FA_ARROWS_TO_DOT " Snap to parent")) {
		object->SnapToAttachParent();
		changed = true;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Clears the relative transform: the object sits exactly on its attach point,\nunrotated and unscaled relative to it.");
	}
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_ARROWS_TO_DOT " Snap (keep scale)")) {
		object->SnapToAttachParent(true);
		changed = true;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Same, but the object keeps its own relative scale — reset placement only.");
	}
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_LINK_SLASH " Detach")) {
		object->DetachFromParent(EAttachmentRule::KeepWorld);
		// Unlike the combos above (which keep the same parent OBJECT and so leave the outliner's row
		// order alone), detaching moves the row back to the root level.
		NotifyAttachmentChanged();
		changed = true;
	}
	ImGui::Separator();
	return changed;
}

// Returns true when the drawn subtree changed the object's attachment layout (asset needs saving).
bool WorldComponentTree(const Plu::TUsePointer<Plu::GameObject>& owner, const Plu::TUsePointer<Plu::WorldComponent>& component)
{
	bool changed = false;
	ImGuiTreeNodeFlags flags = 0;
	if (component->GetChildren().IsEmpty()) {
		flags = ImGuiTreeNodeFlags_Leaf;
	}
	const bool open = ImGui::TreeNodeEx(std::format("{} ({})", component->GetComponentName().CStr(), component->GetClass()->TypeName.CStr()).c_str(), flags);

	// Both hooks go on the tree node itself, so they must be queried before TreePop and before any
	// child item is submitted — hence outside the `if (open)` body.
	if (ImGui::BeginDragDropSource()) {
		const Plu::EngineObjectHandle handle = *component->GetEngineObjectHandle();
		ImGui::SetDragDropPayload(Plu::CSceneComponentDragPayload, &handle, sizeof(handle));
		ImGui::TextUnformatted(component->GetComponentName().CStr());
		ImGui::EndDragDropSource();
	}
	changed |= WorldComponentAttachTarget(owner, component.GetRaw());

	// Same two resets as the object's Attachment section above, plus a way back out of an attachment
	// that does not involve dragging the row onto the object header.
	if (ImGui::BeginPopupContextItem()) {
		gEditorAppContext->EditorState.SelectedGameObjectComponent = *component->GetEngineObjectHandle();
		if (ImGui::Button(ICON_FA_ARROWS_TO_DOT " Snap to parent")) {
			component->SnapToAttachParent();
			changed = true;
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::Button(ICON_FA_ARROWS_TO_DOT " Snap (keep scale)")) {
			component->SnapToAttachParent(true);
			changed = true;
			ImGui::CloseCurrentPopup();
		}
		if (component->GetParentComponent() && ImGui::Button(ICON_FA_LINK_SLASH " Detach")) {
			component->Detach(Plu::EAttachmentRule::KeepWorld);
			changed = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::Separator();
		if (ImGui::Button("Close")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (open) {
		if (ImGui::IsItemClicked()) {
			gEditorAppContext->EditorState.SelectedGameObjectComponent = *component->GetEngineObjectHandle();
		}
		// Copied out of GetChildren(): a drop inside the loop reparents a component and mutates the
		// list the loop walks.
		for (const auto& comp : component->GetChildren()) {
			if (!comp) continue;
			changed |= WorldComponentTree(owner, comp);
		}
		ImGui::TreePop();
	}
	return changed;
}

void Plu::SceneInspectorPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel())
	{
		TUsePointer<SceneInfo> scene = gEditorAppContext->EditorAssetManager->GetAssetData(GetParentViewport()->GetAssetDescriptor());
		if (scene && gEditorAppContext->EditorScenesManager->IsAnySceneOpen() && gEngineObjectManager->IsValid(gEditorAppContext->EditorState.SelectedGameObject))
		{
			TUsePointer<GameObject> gameObj = gEngineObjectManager->GetObjectAsUser<GameObject>(gEditorAppContext->EditorState.SelectedGameObject);

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0,1,0,1));
			bool open = ImGui::BeginMenu(ICON_FA_PLUS "");
			ImGui::PopStyleColor();
			if (open) {
				static TypeInfo* selected = nullptr;
				if (ImGuiWidgets::TypeTree(GameObjectComponent::GetStaticClass(), &selected, "Add Component")) {
					gameObj->AddComponent(selected, selected->TypeName + "New");
					selected = nullptr;
					if (!gEditorAppContext->EditorScenesManager->IsInPIE()) {
						PanelChangedAsset();
					}
				}
				ImGui::EndMenu();
			}

			//Component Tree
			// Drag a component onto another one to attach it, or onto this row to detach it back
			// onto the object itself.
			bool attachmentChanged = ObjectAttachmentSection(gameObj);
			if (ImGui::Selectable(std::format(ICON_FA_SITEMAP " {}", gameObj->GetObjectName().CStr()).c_str(), false)) {
				gEditorAppContext->EditorState.SelectedGameObjectComponent = EngineObjectHandle();
			}
			attachmentChanged |= WorldComponentAttachTarget(gameObj, nullptr);
			for (const auto& worldComp : gameObj->GetDirectlyAttachedWorldComponents()) {
				if (!worldComp) continue;
				attachmentChanged |= WorldComponentTree(gameObj, worldComp);
			}
			if (attachmentChanged && !gEditorAppContext->EditorScenesManager->IsInPIE()) {
				PanelChangedAsset();
			}
			ImGui::Separator();
			for (const auto& comp : *gameObj->GetObjectComponents()) {
				if (ImGui::Selectable(std::format("{} ({})", comp->GetComponentName().CStr(), comp->GetClass()->TypeName.CStr()).c_str())) {
					gEditorAppContext->EditorState.SelectedGameObjectComponent = *comp->GetEngineObjectHandle();
				}
			}
			ImGui::Separator();
			EngineObject* obj = nullptr;
			if (gEngineObjectManager->IsValid(gEditorAppContext->EditorState.SelectedGameObjectComponent)) {
				obj = gEngineObjectManager->GetObjectAsUser<EngineObject>(gEditorAppContext->EditorState.SelectedGameObjectComponent).GetRaw();
				if (obj->GetClass()->IsDerivedOfOrSame(WorldComponent::GetStaticClass())) {
					Vec3 location = dynamic_cast<WorldComponent*>(obj)->GetRelativeLocation();
					if (RGBTransformDrag3("R Location", glm::value_ptr(location), 3, 0.1f,nullptr,nullptr,"%.3f",0))
					{
						dynamic_cast<WorldComponent*>(obj)->SetRelativeLocation(location);
						if (!gEditorAppContext->EditorScenesManager->IsInPIE()) {
							PanelChangedAsset();
						}
					}
					Vec3 rotation = dynamic_cast<WorldComponent*>(obj)->GetRelativeRotation();
					if (RGBTransformDrag3("R Rotation", glm::value_ptr(rotation), 3, 0.1f,nullptr,nullptr,"%.3f",0))
					{
						dynamic_cast<WorldComponent*>(obj)->SetRelativeRotation(rotation);
						if (!gEditorAppContext->EditorScenesManager->IsInPIE()) {
							PanelChangedAsset();
						}
					}
					Vec3 scale = dynamic_cast<WorldComponent*>(obj)->GetRelativeScale();
					if (RGBTransformDrag3("R Scale", glm::value_ptr(scale), 3, 0.1f,nullptr,nullptr,"%.3f",0))
					{
						dynamic_cast<WorldComponent*>(obj)->SetRelativeScale(scale);
						if (!gEditorAppContext->EditorScenesManager->IsInPIE()) {
							PanelChangedAsset();
						}
					}
				}
			} else {
				obj = gEngineObjectManager->GetObjectAsUser<EngineObject>(gEditorAppContext->EditorState.SelectedGameObject).GetRaw();
				Vec3 location = dynamic_cast<GameObject*>(obj)->GetObjectLocation();
				if (RGBTransformDrag3("Location", glm::value_ptr(location), 3, 0.1f,nullptr,nullptr,"%.3f",0))
				{
					dynamic_cast<GameObject*>(obj)->SetObjectLocation(location);
					if (!gEditorAppContext->EditorScenesManager->IsInPIE()) {
						PanelChangedAsset();
					}
				}
				Vec3 rotation = dynamic_cast<GameObject*>(obj)->GetObjectRotation();
				if (RGBTransformDrag3("Rotation", glm::value_ptr(rotation), 3, 0.1f,nullptr,nullptr,"%.3f",0))
				{
					dynamic_cast<GameObject*>(obj)->SetObjectRotation(rotation);
					if (!gEditorAppContext->EditorScenesManager->IsInPIE()) {
						PanelChangedAsset();
					}
				}
				Vec3 scale = dynamic_cast<GameObject*>(obj)->GetObjectScale();
				if (RGBTransformDrag3("Scale", glm::value_ptr(scale), 3, 0.1f,nullptr,nullptr,"%.3f",0))
				{
					dynamic_cast<GameObject*>(obj)->SetObjectScale(scale);
					if (!gEditorAppContext->EditorScenesManager->IsInPIE()) {
						PanelChangedAsset();
					}
				}
			}
			// mObjectName jest PLU_PROPERTY, więc da się je zmienić tu, z pominięciem
			// SetObjectName. Structure panel cache'uje posortowaną listę nazw, więc taka
			// zmiana musi go dobić eventem — inaczej pokazywałby starą nazwę.
			GameObject* namedObject = dynamic_cast<GameObject*>(obj);
			const String nameBefore = namedObject ? namedObject->GetObjectName() : String();

			if (obj->GetClass() == ParticleSpawnerComponent::GetStaticClass()) {
				if (ImGui::Button("Spawn Particles")) {
					ParticleSpawnerComponent* spawnerComponent = dynamic_cast<ParticleSpawnerComponent *>(obj);
					spawnerComponent->SpawnParticles(spawnerComponent->NumParticlesToSpawn);
				}
			}

			if (TypeSerializer<TypeInfo*>::EditorControl(obj->GetClass(), obj) && !gEditorAppContext->EditorScenesManager->IsInPIE()) {
				PanelChangedAsset();
			}

			if (namedObject && namedObject->GetObjectName() != nameBefore) {
				TUsePointer<SceneWorld> world = gEditorAppContext->EditorScenesManager->GetCurrentWorld();
				if (world) {
					world->GetObjectEventDispatcher()->Dispatch("GameObjectsChanged", nullptr);
				}
			}
		}
	}
	EndPanel();
}
