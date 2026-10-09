//
// Created by Plutex on 1/11/26.
//

#ifndef PLUENGINE_GAMEOBJECT_H
#define PLUENGINE_GAMEOBJECT_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/Reflection/ClassPointer.h"
#include "PluEngine/Gameplay/GameObjectComponent.h"
#include "GameObject.generated.h"
#include "WorldComponent.h"
#include "PluEngine/Gameplay/Components/SkeletalMeshComponent.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"

namespace Plu
{
	class PhysicsCompoundShape;
	class PhysicsBody;
	class GameObjectComponent;
	class InputHandler;
	class WorldComponent;
	class ShaderProgram;
}

namespace Plu
{
	PLU_CLASS(PyExport, PyDerive)
	class PLUGAMEPLAY_API GameObject : public EngineObject
	{
		REFLECTION_BODY_GAMEOBJECT()
	private:
		// Transform in the attach parent's space. For an unattached object — which is every object
		// in a scene that uses no attachments — the parent frame is identity, so these ARE the world
		// values and everything behaves exactly like it did before attachments existed.
		Vec3 mLocation = Vec3(0);
		Vec3 mRotation = Vec3(0);
		Vec3 mScale = Vec3(1);

		TOwningPointer<PhysicsCompoundShape> mCompoundShape;
		EngineObjectHandle mPhysicsBodyHandle;

		// attachParentWorld * translate(mLocation) * rotate(mRotation) * scale(mScale), rebuilt
		// lazily. Invalidated by this object's own setters and pushed down from whatever it is
		// attached to (WorldComponent::MarkWorldMatrixForRegeneration / the parent object's setters).
		mutable Matrix4 mWorldMatrix = glm::identity<Matrix4>();
		mutable bool mRegenerateWorldMatrix = true;
		// Decomposition of mWorldMatrix, refreshed together with it. While unattached these are
		// copied straight from the fields above instead of being decomposed, so an object that never
		// gets attached reports back exactly the euler angles that were set on it.
		mutable Vec3 mWorldLocation = Vec3(0);
		mutable Vec3 mWorldRotation = Vec3(0);
		mutable Vec3 mWorldScale = Vec3(1);

		// Attachment (UE's AActor::AttachToComponent): the object rides either a WorldComponent of
		// another object — optionally a named socket/attach point on it — or another object's own
		// transform. At most one of the two is ever set.
		TUsePointer<WorldComponent> mAttachParentComponent;
		TUsePointer<GameObject> mAttachParentObject;
		String mAttachSocketName;
		// Objects attached to THIS object's transform (not to one of its components — those are
		// tracked by the component). Non-owning, see WorldComponent::mAttachedObjects.
		DynamicArray<TUsePointer<GameObject>> mAttachedObjects;

		// Full world frame this object's local transform is applied on top of: the socket frame when
		// a socket is set and its pose is available, otherwise the attach parent component's world
		// matrix, otherwise the attach parent object's world matrix, otherwise identity.
		[[nodiscard]] Matrix4 GetAttachParentWorldMatrix() const;
		void RefreshWorldTransform() const;
		// Bookkeeping shared by every attach/detach path: unregisters from the current parent's child
		// list, stores the new parent, registers there. Applies no transform rule.
		void SetAttachParentInternal(const TUsePointer<WorldComponent>& parentComponent, const TUsePointer<GameObject>& parentObject, const String& socketName);
		// Common tail of AttachToComponent/AttachToObject/DetachFromParent: rule handling around the
		// SetAttachParentInternal call, cycle/self guards, invalidation. Returns false when rejected.
		bool ChangeAttachment(const TUsePointer<WorldComponent>& parentComponent, const TUsePointer<GameObject>& parentObject, const String& socketName, EAttachmentRule rule);

		PluUUID mUuid;

		// Trwała nazwa obiektu w scenie. Domyślnie nadawana przez SceneWorld::MakeDefaultObjectName
		// (TypeName + najniższy wolny indeks w tej scenie), potem edytowalna w Structure panelu.
		// Jest PLU_PROPERTY, więc jedzie w JSON-ie sceny razem z resztą właściwości — dzięki temu
		// przeżywa PIE (które zapisuje i przeładowuje scenę) i restart edytora. Nie mylić z
		// EngineObject::GetDisplayName(), które używa procesowego short-term ID i rośnie w nieskończoność.
		PLU_PROPERTY()
		String mObjectName;

		DynamicArray<TOwningPointer<GameObjectComponent>> mComponents;
		DynamicArray<TOwningPointer<WorldComponent>> mWorldComponents;
		bool mRedoWorldComponentList = true;

		DynamicArray<TUsePointer<WorldComponent>> mCachedWorldComponents;

		TUsePointer<class SceneWorld> mWorld;
		TUsePointer<class EngineObjectManager> mObjectManager;

		float mLifetime = 0.0f;
		float mElapsedLifetime = 0.0f;

		friend class GameObjectComponent;
		friend class WorldComponent;
		friend class SceneWorld;
		friend class PhysicsWorld;

		void InitGameObject(const TUsePointer<class SceneWorld>& sceneWorld, const TUsePointer<class EngineObjectManager>& objectManager);
		// Attachment bookkeeping, driven by WorldComponent::AttachTo. The owning pointer to a world
		// component lives in exactly one list: its attach point's mWorldComponents, or this object's
		// when it has none. Detach takes the OLD attach point because the component's mParentComponent
		// has to be updated between the two calls.
		void OnAttachComponent(const TOwningPointer<WorldComponent>& component, const TUsePointer<WorldComponent>& attachPoint);
		void OnDetachComponent(const TOwningPointer<WorldComponent>& component, const TUsePointer<WorldComponent>& attachPoint);

		bool DeleteGameObjectComponentImpl(TUsePointer<GameObjectComponent> component);
		bool DeleteWorldComponentImpl(TUsePointer<WorldComponent> component);
	protected:
		TUsePointer<GameObject> This();
		TUsePointer<SceneWorld> GetWorld();
	public:
		GameObject() = default;
		virtual ~GameObject() override;

		PLU_FUNCTION(PyOverride)
		virtual void OnSetupComponents() {}

		PLU_FUNCTION(PyOverride)
		virtual void OnBeginPlay() {}

		PLU_FUNCTION(PyOverride)
		virtual void OnUpdate(float deltaTime) {}

		PLU_FUNCTION(PyOverride)
		virtual void OnEndPlay() {}

		PLU_FUNCTION(PyOverride)
		virtual void OnOverlapBegin(TUsePointer<WorldComponent> component, TUsePointer<GameObject> otherObject, TUsePointer<WorldComponent> otherComponent) {}

		PLU_FUNCTION(PyOverride)
		virtual void OnOverlapEnd(TUsePointer<WorldComponent> component, TUsePointer<GameObject> otherObject, TUsePointer<WorldComponent> otherComponent) {}

		PLU_FUNCTION(PyNotCallable)
		void Cleanup();

		void TickObject(float deltaTime);

		PLU_FUNCTION()
		TUsePointer<GameObjectComponent> AddComponent(TClassPointer<GameObjectComponent> componentClass, String componentName);
		void RegisterComponent(EngineObjectHandle component);
		PLU_FUNCTION()
		bool DeleteComponent(GameObjectComponent* component);

		PLU_FUNCTION()
		DynamicArray<TOwningPointer<GameObjectComponent>>* GetObjectComponents();

		PLU_FUNCTION()
		DynamicArray<TUsePointer<WorldComponent>>* GetObjectWorldComponents(bool force = false);
		PLU_FUNCTION()
		DynamicArray<TUsePointer<WorldComponent>> GetDirectlyAttachedWorldComponents();

		PLU_FUNCTION()
		TUsePointer<GameObjectComponent> GetComponentByClass(const TClassPointer<GameObjectComponent>& componentClass);
		PLU_FUNCTION()
		DynamicArray<TUsePointer<GameObjectComponent>> GetAllComponentsByClass(const TClassPointer<GameObjectComponent>& componentClass);

		// Lookup by GameObjectComponent::Uuid. Takes a plain UInt64 so Python can call it (PluUUID
		// converts implicitly). GetComponentByUUID searches only the plain (non-world) component
		// list; GetWorldComponentByUUID searches the flattened world component tree, so a component
		// attached under another one is found too. Null when nothing matches or the UUID is invalid (0).
		PLU_FUNCTION()
		TUsePointer<GameObjectComponent> GetComponentByUUID(UInt64 uuid);
		PLU_FUNCTION()
		TUsePointer<WorldComponent> GetWorldComponentByUUID(UInt64 uuid);

		/** Trwała nazwa obiektu w scenie (patrz mObjectName). Pusta tylko dla obiektów
		 *  utworzonych z pominięciem SceneWorld::SpawnGameObject. */
		PLU_FUNCTION(PyExport)
		[[nodiscard]] const String& GetObjectName() const { return mObjectName; }
		PLU_FUNCTION(PyExport)
		void SetObjectName(const String& name) { mObjectName = name; }

		// WORLD transform — the meaning these have always had, and what physics, the renderer, the
		// gizmo and gameplay code all expect. For an unattached object they return the stored fields
		// verbatim; for an attached one they are read off the resolved world matrix.
		PLU_FUNCTION()
		[[nodiscard]] Vec3 GetObjectLocation() const;
		PLU_FUNCTION()
		[[nodiscard]] Vec3 GetObjectRotation() const;
		PLU_FUNCTION()
		[[nodiscard]] Vec3 GetObjectScale() const;

		[[nodiscard]] Matrix4 GetObjectWorldMatrix() const;

		// Setting a WORLD transform on an attached object folds the value back into the parent's
		// space, so the object really lands where it was asked to.
		PLU_FUNCTION()
		void SetObjectLocation(const Vec3& location);
		PLU_FUNCTION()
		void SetObjectRotation(const Vec3& rotation);
		PLU_FUNCTION()
		void SetObjectScale(const Vec3& scale);

		// Transform in the attach parent's space — the offset from the socket/component/object this
		// object rides. Identical to the world transform while unattached, and this is what scene
		// serialization stores.
		PLU_FUNCTION()
		[[nodiscard]] Vec3 GetRelativeLocation() const { return mLocation; }
		PLU_FUNCTION()
		[[nodiscard]] Vec3 GetRelativeRotation() const { return mRotation; }
		PLU_FUNCTION()
		[[nodiscard]] Vec3 GetRelativeScale() const { return mScale; }

		PLU_FUNCTION()
		void SetRelativeLocation(const Vec3& location);
		PLU_FUNCTION()
		void SetRelativeRotation(const Vec3& rotation);
		PLU_FUNCTION()
		void SetRelativeScale(const Vec3& scale);

		// Invalidates this object's world matrix, every component hanging off it, and every object
		// attached to it. Public because the render snapshot builder has to trigger it for objects
		// riding a skeletal socket — a new pose changes their parent frame without any setter call.
		void MarkWorldMatrixForRegeneration();

		PLU_FUNCTION()
		[[nodiscard]] Vec3 GetObjectForwardVector() const;
		PLU_FUNCTION()
		[[nodiscard]] Vec3 GetObjectRightVector() const;
		PLU_FUNCTION()
		[[nodiscard]] Vec3 GetObjectUpVector() const;

		PLU_FUNCTION()
		PluUUID& GetObjectUUID();


		// --- Attachment (UE: AActor::AttachToComponent / AttachToActor / DetachFromActor) ---------
		// Rides parentComponent, optionally a named socket (a skeletal mesh attach point) on it.
		// The object keeps its own lifetime and its own physics body; only its transform becomes
		// a child of the parent's. Rejected with an error log, leaving the attachment untouched:
		// a null parent (use DetachFromParent), one of this object's own components, or a parent
		// that already rides this object — all three would build a transform cycle.
		PLU_FUNCTION()
		void AttachToComponent(WorldComponent* parentComponent, const String& socketName = "", EAttachmentRule rule = EAttachmentRule::KeepRelative);
		// Rides another object's transform directly. PluEngine's GameObject has no single root
		// component like AActor does, so this is the plain object-to-object parenting the editor
		// outliner needs; UE would express it as AttachToActor with the root component.
		PLU_FUNCTION()
		void AttachToObject(GameObject* parentObject, EAttachmentRule rule = EAttachmentRule::KeepRelative);
		PLU_FUNCTION()
		void DetachFromParent(EAttachmentRule rule = EAttachmentRule::KeepWorld);
		// Drops the offset from the attach parent: the object sits exactly on it (or on its socket),
		// unrotated relative to it — the same end state an attach with EAttachmentRule::SnapToTarget
		// produces. keepScale leaves the relative scale alone, for the common case where the object
		// is authored at a size of its own and only its placement should be reset. No-op when
		// unattached.
		PLU_FUNCTION()
		void SnapToAttachParent(bool keepScale = false);

		PLU_FUNCTION()
		[[nodiscard]] bool IsAttached() const;
		// The component this object rides, or null when it rides an object directly (or nothing).
		PLU_FUNCTION()
		[[nodiscard]] TUsePointer<WorldComponent> GetAttachParentComponent() const;
		// The object this object rides — the owner of the attach parent component, or the directly
		// attached-to object. Null when unattached.
		PLU_FUNCTION()
		[[nodiscard]] TUsePointer<GameObject> GetAttachParentObject() const;
		PLU_FUNCTION()
		[[nodiscard]] const String& GetAttachSocketName() const { return mAttachSocketName; }
		// Objects riding this object's own transform (not those riding its components — ask the
		// component for those).
		PLU_FUNCTION()
		[[nodiscard]] DynamicArray<TUsePointer<GameObject>> GetAttachedObjects() const;
		// True when object is this object's attach parent, its parent's parent, … Walks through both
		// kinds of link (component and object) and is what guards the attach calls against cycles.
		PLU_FUNCTION()
		[[nodiscard]] bool IsAttachedToObject(GameObject* object) const;

		// Thin wrapper kept for existing content: attaches to a skeletal mesh attach point with
		// SnapToTarget, which is what this call used to do unconditionally (it hard-set the object's
		// location and rotation to the attach point's every frame, so an offset was impossible).
		// New code can use AttachToComponent directly and keep an offset from the socket.
		PLU_FUNCTION()
		void AttachToSkeletalMeshComponent(SkeletalMeshComponent* skeletalMeshComponent, const String &attachPointName);
		PLU_FUNCTION()
		void DetachFromSkeletalMeshComponent();
		PLU_FUNCTION()
		[[nodiscard]] bool IsAttachedToSkeletalMesh() const;

		// The skeletal mesh this object rides through a socket, or null. Lets the render snapshot
		// builder find the objects whose parent frame is a bone: their world transform has to be
		// invalidated after the pose for THIS frame is evaluated, otherwise everything riding a bone
		// is one frame behind it (which is exactly what used to happen when this was resolved from
		// GameObject::TickObject).
		[[nodiscard]] TUsePointer<SkeletalMeshComponent> GetSkeletalAttachmentParent() const;

		PLU_FUNCTION()
		float GetElapsedTimeSinceSpawn() const;
		PLU_FUNCTION()
		void SetLifetime(float lifetime);


		// Whether this object's physics body simulates (Dynamic) or is Static. A JPH::Body has a
		// single motion type for the whole compound shape, so this is a per-object property, not a
		// per-PhysicsBodyComponent one (friction/restitution/channel live on the components/materials).
		PLU_PROPERTY(PyExport)
		bool ActiveBody = false;

		virtual InputHandler* GetInputHandler() {return nullptr;};
	};

	template<>
	struct TypeSerializer<GameObject>
	{
		static nlohmann::json Serialize(void* dataToSerialize)
		{
			GameObject* obj = static_cast<GameObject *>(dataToSerialize);
			JSON j = TypeSerializer<TypeInfo *>::Serialize(obj->GetClass(), obj);
			// Relative, not world: for an unattached object the two are the same value, and for an
			// attached one the offset from its parent is the thing worth storing (the world position
			// is rebuilt from the parent chain on load).
			Vec3 loc = obj->GetRelativeLocation();
			j["location"] = TypeSerializer<glm::vec3>::Serialize(&loc);
			Vec3 rot = obj->GetRelativeRotation();
			j["rotation"] = TypeSerializer<glm::vec3>::Serialize(&rot);
			Vec3 scl = obj->GetRelativeScale();
			j["scale"] = TypeSerializer<glm::vec3>::Serialize(&scl);
			j["uuid"] = TypeSerializer<PluUUID>::Serialize(&obj->GetObjectUUID());
			// The parent is referenced by UUID because it may well be deserialized after this object;
			// SceneManager resolves the whole batch once every object exists.
			if (TUsePointer<GameObject> attachParent = obj->GetAttachParentObject()) {
				JSON attachment;
				attachment["parentUuid"] = TypeSerializer<PluUUID>::Serialize(&attachParent->GetObjectUUID());
				attachment["componentName"] = obj->GetAttachParentComponent()
					? obj->GetAttachParentComponent()->GetComponentName().CStr() : "";
				attachment["socket"] = obj->GetAttachSocketName().CStr();
				j["attachment"] = attachment;
			}
			j["typeName"] = obj->GetClass()->TypeName.CStr();
			j["worldComponents"] = nlohmann::json::array();
			j["components"] = nlohmann::json::array();
			for (const auto& worldComp : obj->GetDirectlyAttachedWorldComponents()) {
				//TypeSerializer<TypeInfo*>::Serialize(worldComp->GetClass(), worldComp.GetRaw())
				j["worldComponents"].push_back(worldComp->Serialize());
			}
			for (const auto& comp : *obj->GetObjectComponents()) {
				j["components"].push_back(TypeSerializer<TypeInfo*>::Serialize(comp->GetClass(), comp.GetRaw()));
			}
			return j;
		}

		static void Deserialize(DeserializationContext* dc, const nlohmann::json& json, void* outValue)
		{
		}

		static void EditorControl(void* value, const String& name)
		{

		}
	};
}

#endif //PLUENGINE_GAMEOBJECT_H
