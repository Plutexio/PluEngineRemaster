//
// Created by Plutex on 1/18/26.
//

#include "PluEngine/Gameplay/GameObject.h"

#include "PluEngine/PluUtils.h"
#include "PluEngine/Gameplay/GameObjectComponent.h"
#include "PluEngine/Gameplay/WorldComponent.h"
#include "PluEngine/Gameplay/InputHandler.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"

void Plu::GameObject::InitGameObject(const TUsePointer<class SceneWorld> &sceneWorld,
                                     const TUsePointer<class EngineObjectManager> &objectManager)
{
	mObjectManager = objectManager;
	mWorld = sceneWorld;
}

void Plu::GameObject::OnAttachComponent(const TOwningPointer<WorldComponent>& component,
	const TUsePointer<WorldComponent>& attachPoint)
{
	mRedoWorldComponentList = true;
	DynamicArray<TOwningPointer<WorldComponent>>& target = attachPoint ? attachPoint->mWorldComponents : mWorldComponents;
	if (!target.Contains(component)) {
		target.PushBack(component);
	}
}

void Plu::GameObject::OnDetachComponent(const TOwningPointer<WorldComponent>& component,
	const TUsePointer<WorldComponent>& attachPoint)
{
	mRedoWorldComponentList = true;
	if (attachPoint) {
		attachPoint->mWorldComponents.Remove(component);
	} else {
		mWorldComponents.Remove(component);
	}
}

Plu::TUsePointer<Plu::GameObject> Plu::GameObject::This()
{
	return mObjectManager->GetObjectAsUser<GameObject>(*GetEngineObjectHandle());
}

Plu::TUsePointer<Plu::SceneWorld> Plu::GameObject::GetWorld()
{
	return mWorld;
}

Plu::GameObject::~GameObject()
{
}

void Plu::GameObject::Cleanup()
{
	// Attachments never own anything, so a destroyed object drops out of its parent's child list and
	// hands its own children back to the world (UE does the same: children of a destroyed actor are
	// detached, not destroyed). KeepWorld so nothing teleports on the way out.
	DetachFromParent(EAttachmentRule::KeepWorld);
	DynamicArray<TUsePointer<GameObject>> attachedObjects = mAttachedObjects;
	for (const auto& attached : attachedObjects) {
		if (!attached) continue;
		attached->DetachFromParent(EAttachmentRule::KeepWorld);
	}
	for (auto component : mComponents) {
		mObjectManager->DestroyObject(*component->GetEngineObjectHandle());
	}
	for (const auto& component : mWorldComponents) {
		component->Cleanup();
		mObjectManager->DestroyObject(*component->GetEngineObjectHandle());
	}
	mComponents.Clear();
	mWorldComponents.Clear();
	mObjectManager->DestroyObject(mPhysicsBodyHandle);
	mCompoundShape = nullptr;
}

void Plu::GameObject::TickObject(float deltaTime)
{
	if (GetInputHandler()) {
		GetInputHandler()->TickHandler();
	}
	// NOTE: attachments are not resolved here — an attached object's world transform is pulled from
	// its parent chain on demand (GetObjectWorldMatrix), and bone sockets are invalidated by the
	// render snapshot builder once the parent's pose for this frame exists.
	try
	{
		OnUpdate(deltaTime);
	} catch (pybind11::error_already_set& e)
	{
		PLU_CORE_ERROR("Error In Python {}", e.what());
	}
	// The flattened list, not mWorldComponents: the latter holds only the components attached
	// directly to the object, so nested attachments would never tick. Indexed and re-fetched per
	// step because OnUpdate may add or reparent a component, which rebuilds (and reallocates) the
	// cached array.
	for (size_t i = 0; i < GetObjectWorldComponents()->Size(); ++i) {
		TUsePointer<WorldComponent> worldComp = (*GetObjectWorldComponents())[i];
		if (!worldComp) continue;
		worldComp->OnUpdate(deltaTime);
	}
	for (const auto& comp : mComponents) {
		comp->OnUpdate(deltaTime);
	}

	if (mLifetime != 0) {
		mElapsedLifetime += deltaTime;
		if (mElapsedLifetime >= mLifetime) {
			GetWorld()->DeleteGameObject(GetObjectHandle());
		}
	}
}

Plu::TUsePointer<Plu::GameObjectComponent> Plu::GameObject::AddComponent(TClassPointer<GameObjectComponent> componentClass, String componentName)
{
	if (!componentClass.GetRawType()) return nullptr;
	PLU_CORE_ASSERT(componentClass.GetRawType()->IsDerivedOfOrSame(GameObjectComponent::GetStaticClass()), "Tried to create new component with invalid Component Class! Possibly class is not derived from GameObjectComponent")
	TUsePointer<GameObjectComponent> newComponent = mObjectManager->CreateObject(componentClass);
	newComponent->mComponentName = componentName;
	RegisterComponent(*newComponent->GetEngineObjectHandle());
	return newComponent;
}

void Plu::GameObject::RegisterComponent(EngineObjectHandle component)
{
	TOwningPointer<GameObjectComponent> newComponent = mObjectManager->GetObjectAsOwner<GameObjectComponent>(component);
	if (newComponent->GetClass()->IsDerivedOfOrSame(WorldComponent::GetStaticClass())) {
		mWorldComponents.PushBack(newComponent);
		mRedoWorldComponentList = true;
	} else {
		mComponents.PushBack(newComponent);
	}
	newComponent->SetParentGameObject(mObjectManager->GetObjectAsUser<GameObject>(*GetEngineObjectHandle()));
	mWorld->NewGameObjectComponent(newComponent);
}

bool Plu::GameObject::DeleteComponent(GameObjectComponent *component)
{
	if (!component) return false;
	EngineObjectHandle componentHandle = component->GetObjectHandle();
	if (!mObjectManager->IsValid(componentHandle)) return false;
	PLU_CORE_TRACE("Destroy GameObjectComponent, name {}", component->GetDisplayName().CStr());
	if (component->GetClass()->IsDerivedOfOrSame(WorldComponent::GetStaticClass())) {
		return DeleteWorldComponentImpl(mObjectManager->GetObjectAsUser<WorldComponent>(componentHandle));
	}
	return DeleteGameObjectComponentImpl(mObjectManager->GetObjectAsUser<GameObjectComponent>(componentHandle));
}

DynamicArray<Plu::TOwningPointer<Plu::GameObjectComponent>> * Plu::GameObject::GetObjectComponents()
{
	return &mComponents;
}

void GatherWorldComponentChildren(DynamicArray<Plu::TUsePointer<Plu::WorldComponent>>* components, Plu::TUsePointer<Plu::WorldComponent> component)
{
	components->Append(component->GetChildren());
	for (const auto& child : component->GetChildren()) {
		GatherWorldComponentChildren(components, child);
	}
}

DynamicArray<Plu::TUsePointer<Plu::WorldComponent>>* Plu::GameObject::GetObjectWorldComponents(bool force)
{
	if (mRedoWorldComponentList || force) {
		mCachedWorldComponents.Clear();
		for (const auto& worldComp : mWorldComponents) {
			mCachedWorldComponents.PushBack(worldComp);
			GatherWorldComponentChildren(&mCachedWorldComponents, worldComp);
		}
		mRedoWorldComponentList = false;
	}
	return &mCachedWorldComponents;
}

DynamicArray<Plu::TUsePointer<Plu::WorldComponent>> Plu::GameObject::GetDirectlyAttachedWorldComponents()
{
	DynamicArray<TUsePointer<WorldComponent>> components(mWorldComponents.Size());
	for (const auto& comp : mWorldComponents) {
		components.PushBack(comp);
	}
	return components;
}

Plu::TUsePointer<Plu::GameObjectComponent> Plu::GameObject::GetComponentByClass(
	const TClassPointer<GameObjectComponent>& componentClass)
{
	if (componentClass.GetRawType()->IsDerivedOfOrSame(WorldComponent::GetStaticClass())) {
		// Flattened: a component attached under another one is still a component of this object.
		for (const auto& worldComp : *GetObjectWorldComponents()) {
			if (worldComp->GetClass()->IsDerivedOfOrSame(componentClass)) {
				return worldComp;
			}
		}
	} else {
		for (const auto& comp : mComponents) {
			if (comp->GetClass()->IsDerivedOfOrSame(componentClass)) {
				return comp;
			}
		}
	}
	return nullptr;
}

Plu::TUsePointer<Plu::GameObjectComponent> Plu::GameObject::GetComponentByUUID(UInt64 uuid)
{
	if (uuid == 0) {
		return nullptr;
	}
	for (const auto& comp : mComponents) {
		if (comp->Uuid == uuid) {
			return comp;
		}
	}
	return nullptr;
}

Plu::TUsePointer<Plu::WorldComponent> Plu::GameObject::GetWorldComponentByUUID(UInt64 uuid)
{
	if (uuid == 0) {
		return nullptr;
	}
	// Flattened: a component attached under another one is still a component of this object.
	for (const auto& worldComp : *GetObjectWorldComponents()) {
		if (worldComp->Uuid == uuid) {
			return worldComp;
		}
	}
	return nullptr;
}

DynamicArray<Plu::TUsePointer<Plu::GameObjectComponent>> Plu::GameObject::GetAllComponentsByClass(
	const TClassPointer<GameObjectComponent>& componentClass)
{
	if (componentClass.GetRawType()->IsDerivedOfOrSame(WorldComponent::GetStaticClass())) {

		DynamicArray<TUsePointer<GameObjectComponent>> components;
		for (const auto& worldComp : *GetObjectWorldComponents()) {
			if (worldComp->GetClass()->IsDerivedOfOrSame(componentClass)) {
				components.PushBack(worldComp);
			}
		}
		return components;
	}
	DynamicArray<TUsePointer<GameObjectComponent>> components;
	for (const auto& comp : mComponents) {
		if (comp->GetClass()->IsDerivedOfOrSame(componentClass)) {
			components.PushBack(comp);
		}
	}
	return components;
}

Vec3 Plu::GameObject::GetObjectLocation() const
{
	RefreshWorldTransform();
	return mWorldLocation;
}

Vec3 Plu::GameObject::GetObjectRotation() const
{
	RefreshWorldTransform();
	return mWorldRotation;
}

Vec3 Plu::GameObject::GetObjectScale() const
{
	RefreshWorldTransform();
	return mWorldScale;
}

Matrix4 Plu::GameObject::GetAttachParentWorldMatrix() const
{
	if (mAttachParentComponent) {
		if (!mAttachSocketName.IsEmpty()) {
			if (auto* skeletalMesh = dynamic_cast<SkeletalMeshComponent*>(mAttachParentComponent.GetRaw())) {
				Matrix4 socketMatrix;
				// False until the parent mesh has a pose (no snapshot built since the mesh was
				// assigned). Falling back to the component frame keeps the object near its parent
				// for those first frames instead of teleporting it to the world origin.
				if (skeletalMesh->TryGetAttachPointWorldMatrix(mAttachSocketName, socketMatrix)) {
					return socketMatrix;
				}
			}
		}
		return mAttachParentComponent->GetWorldMatrix();
	}
	if (mAttachParentObject) {
		return mAttachParentObject->GetObjectWorldMatrix();
	}
	return glm::identity<Matrix4>();
}

void Plu::GameObject::RefreshWorldTransform() const
{
	if (!mRegenerateWorldMatrix) return;
	mRegenerateWorldMatrix = false;

	const Matrix4 localMatrix = glm::translate(glm::mat4(1.0f), mLocation) *
			  glm::mat4_cast(glm::quat(glm::radians(mRotation))) *
			  glm::scale(glm::mat4(1.0f), mScale);

	if (!mAttachParentComponent && !mAttachParentObject) {
		// Unattached: local IS world. The world values are copied rather than decomposed out of the
		// matrix on purpose — decomposing a rotation round-trips through a quaternion and can hand
		// back a different (if equivalent) euler triple, which would make the editor's rotation drag
		// jump under the user's cursor.
		mWorldMatrix = localMatrix;
		mWorldLocation = mLocation;
		mWorldRotation = mRotation;
		mWorldScale = mScale;
		return;
	}

	mWorldMatrix = GetAttachParentWorldMatrix() * localMatrix;
	mWorldLocation = GetLocationFromMatrix(mWorldMatrix);
	mWorldRotation = GetRotationFromMatrix(mWorldMatrix);
	mWorldScale = GetScaleFromMatrix(mWorldMatrix);
}

Matrix4 Plu::GameObject::GetObjectWorldMatrix() const
{
	RefreshWorldTransform();
	return mWorldMatrix;
}

void Plu::GameObject::MarkWorldMatrixForRegeneration()
{
	mRegenerateWorldMatrix = true;
	for (const auto& child : mWorldComponents) {
		child->MarkWorldMatrixForRegeneration();
	}
	// Attachment chains are guarded against cycles at attach time, so this recursion terminates.
	for (const auto& attached : mAttachedObjects) {
		if (!attached) continue;
		attached->MarkWorldMatrixForRegeneration();
	}
}

void Plu::GameObject::SetObjectLocation(const Vec3 &location)
{
	if (IsAttached()) {
		// World -> parent space. The parent frame is resolved first, so this lands the object exactly
		// where the caller asked even mid-chain.
		SetRelativeLocation(Vec3(glm::inverse(GetAttachParentWorldMatrix()) * Vec4(location, 1.0f)));
		return;
	}
	SetRelativeLocation(location);
}

void Plu::GameObject::SetObjectRotation(const Vec3 &rotation)
{
	if (IsAttached()) {
		const Quaternion parentRotation = glm::quat_cast(glm::mat3(GetAttachParentWorldMatrix()));
		const Quaternion localRotation = glm::inverse(glm::normalize(parentRotation)) * Quaternion(glm::radians(rotation));
		SetRelativeRotation(glm::degrees(glm::eulerAngles(localRotation)));
		return;
	}
	SetRelativeRotation(rotation);
}

void Plu::GameObject::SetObjectScale(const Vec3 &scale)
{
	if (IsAttached()) {
		SetRelativeScale(scale / GetScaleFromMatrix(GetAttachParentWorldMatrix()));
		return;
	}
	SetRelativeScale(scale);
}

void Plu::GameObject::SetRelativeLocation(const Vec3 &location)
{
	mLocation = location;
	MarkWorldMatrixForRegeneration();
	GetObjectEventDispatcher()->Dispatch("LocationChange");
}

void Plu::GameObject::SetRelativeRotation(const Vec3 &rotation)
{
	mRotation = rotation;
	NormalizeVec3Rotation(&mRotation);
	MarkWorldMatrixForRegeneration();
	GetObjectEventDispatcher()->Dispatch("RotationChange");
}

void Plu::GameObject::SetRelativeScale(const Vec3 &scale)
{
	mScale = scale;
	MarkWorldMatrixForRegeneration();
	GetObjectEventDispatcher()->Dispatch("ScaleChange");
	// Jolt collision shapes are baked at a fixed scale; rebuild the body so colliders match.
	if (mWorld) {
		mWorld->OnGameObjectScaleChanged(this);
	}
}

bool Plu::GameObject::DeleteGameObjectComponentImpl(TUsePointer<GameObjectComponent> component)
{
	// Owning reference held for the whole call: removing it from mComponents below drops the only
	// other owner, and the world bookkeeping still has to read the component afterwards.
	TOwningPointer<GameObjectComponent> componentOwner = mObjectManager->GetObjectAsOwner<GameObjectComponent>(component->GetObjectHandle());
	if (!componentOwner) return false;

	GetWorld()->DeleteGameObjectComponent(componentOwner);
	const bool removed = mComponents.Remove(componentOwner);
	mObjectManager->DestroyObject(component->GetObjectHandle());

	return removed;
}

bool Plu::GameObject::DeleteWorldComponentImpl(TUsePointer<WorldComponent> component)
{
	TypeInfo* componentClass = component->GetClass();

	PLU_CORE_ASSERT(componentClass, "Component Class is NULL! GameObject::DeleteWorldComponentImpl")

	TOwningPointer<WorldComponent> componentOwner = mObjectManager->GetObjectAsOwner<WorldComponent>(component->GetObjectHandle());
	if (!componentOwner) return false;

	// The children go down with their parent — the owning pointer to each of them lives in the
	// component's own list. WorldComponent::Cleanup destroys that subtree (and hands the objects
	// riding it back to the world), but it knows nothing about the world's side tables, so those are
	// unregistered here, deepest components included.
	DynamicArray<TUsePointer<WorldComponent>> subtree;
	GatherWorldComponentChildren(&subtree, component);
	for (const auto& child : subtree) {
		if (!child) continue;
		GetWorld()->DeleteGameObjectComponent(mObjectManager->GetObjectAsOwner<WorldComponent>(child->GetObjectHandle()));
	}
	GetWorld()->DeleteGameObjectComponent(componentOwner);

	TUsePointer<WorldComponent> parentComponent = component->GetParentComponent();
	if (parentComponent) {
		parentComponent->mWorldComponents.Remove(componentOwner);
	} else {
		mWorldComponents.Remove(componentOwner);
	}
	mRedoWorldComponentList = true;

	component->Cleanup();
	mObjectManager->DestroyObject(component->GetObjectHandle());

	return true;
}

bool Plu::GameObject::IsAttached() const
{
	return mAttachParentComponent.IsValid() || mAttachParentObject.IsValid();
}

Plu::TUsePointer<Plu::WorldComponent> Plu::GameObject::GetAttachParentComponent() const
{
	return mAttachParentComponent;
}

Plu::TUsePointer<Plu::GameObject> Plu::GameObject::GetAttachParentObject() const
{
	if (mAttachParentComponent) return mAttachParentComponent->GetParentGameObject();
	return mAttachParentObject;
}

DynamicArray<Plu::TUsePointer<Plu::GameObject>> Plu::GameObject::GetAttachedObjects() const
{
	return mAttachedObjects;
}

bool Plu::GameObject::IsAttachedToObject(GameObject *object) const
{
	if (!object) return false;
	// Bounded like RenderSnapshotBuilder::GetAttachmentDepth: attach calls guard against cycles, but
	// a chain that got corrupted anyway must not hang the caller.
	constexpr UInt32 maxChain = 64;
	TUsePointer<GameObject> current = GetAttachParentObject();
	for (UInt32 step = 0; current && step < maxChain; ++step) {
		if (current.GetRaw() == object) return true;
		current = current->GetAttachParentObject();
	}
	return false;
}

void Plu::GameObject::SetAttachParentInternal(const TUsePointer<WorldComponent>& parentComponent,
	const TUsePointer<GameObject>& parentObject, const String& socketName)
{
	TUsePointer<GameObject> self = This();
	if (mAttachParentComponent) {
		mAttachParentComponent->mAttachedObjects.Remove(self);
	} else if (mAttachParentObject) {
		mAttachParentObject->mAttachedObjects.Remove(self);
	}

	mAttachParentComponent = parentComponent;
	mAttachParentObject = parentComponent ? nullptr : parentObject;
	mAttachSocketName = parentComponent ? socketName : String();

	if (mAttachParentComponent) {
		mAttachParentComponent->mAttachedObjects.PushBack(self);
	} else if (mAttachParentObject) {
		mAttachParentObject->mAttachedObjects.PushBack(self);
	}
}

bool Plu::GameObject::ChangeAttachment(const TUsePointer<WorldComponent>& parentComponent,
	const TUsePointer<GameObject>& parentObject, const String& socketName, EAttachmentRule rule)
{
	TUsePointer<GameObject> newParentObject = parentComponent ? parentComponent->GetParentGameObject() : parentObject;
	if (newParentObject.GetRaw() == this) {
		PLU_CORE_ERROR("Cannot attach GameObject '{}' to itself (or to one of its own components).", GetObjectName().CStr());
		return false;
	}
	if (newParentObject && newParentObject->IsAttachedToObject(this)) {
		PLU_CORE_ERROR("Cannot attach GameObject '{}' to '{}' — it already rides this object, which would form a cycle.",
			GetObjectName().CStr(), newParentObject->GetObjectName().CStr());
		return false;
	}

	const Matrix4 previousWorldMatrix = (rule == EAttachmentRule::KeepWorld) ? GetObjectWorldMatrix() : glm::identity<Matrix4>();

	SetAttachParentInternal(parentComponent, newParentObject, socketName);
	MarkWorldMatrixForRegeneration();

	switch (rule) {
	case EAttachmentRule::KeepRelative:
		// Nothing to do — the relative transform is already what it should be.
		break;
	case EAttachmentRule::KeepWorld: {
		const Matrix4 localMatrix = glm::inverse(GetAttachParentWorldMatrix()) * previousWorldMatrix;
		mLocation = GetLocationFromMatrix(localMatrix);
		mRotation = GetRotationFromMatrix(localMatrix);
		mScale = GetScaleFromMatrix(localMatrix);
		break;
	}
	case EAttachmentRule::SnapToTarget:
		mLocation = Vec3(0.0f);
		mRotation = Vec3(0.0f);
		mScale = Vec3(1.0f);
		break;
	}
	MarkWorldMatrixForRegeneration();

	return true;
}

void Plu::GameObject::AttachToComponent(WorldComponent *parentComponent, const String &socketName, EAttachmentRule rule)
{
	if (!parentComponent) {
		PLU_CORE_ERROR("AttachToComponent with a null parent on '{}' — use DetachFromParent to clear an attachment.", GetObjectName().CStr());
		return;
	}
	ChangeAttachment(mObjectManager->GetObjectAsUser<WorldComponent>(*parentComponent->GetEngineObjectHandle()), nullptr, socketName, rule);
}

void Plu::GameObject::AttachToObject(GameObject *parentObject, EAttachmentRule rule)
{
	if (!parentObject) {
		PLU_CORE_ERROR("AttachToObject with a null parent on '{}' — use DetachFromParent to clear an attachment.", GetObjectName().CStr());
		return;
	}
	ChangeAttachment(nullptr, mObjectManager->GetObjectAsUser<GameObject>(*parentObject->GetEngineObjectHandle()), String(), rule);
}

void Plu::GameObject::DetachFromParent(EAttachmentRule rule)
{
	if (!IsAttached()) return;
	ChangeAttachment(nullptr, nullptr, String(), rule);
}

void Plu::GameObject::SnapToAttachParent(bool keepScale)
{
	if (!IsAttached()) return;
	// Through the relative setters rather than the fields: they are what pushes the resolved world
	// transform into the physics body and fires the transform events.
	SetRelativeLocation(Vec3(0.0f));
	SetRelativeRotation(Vec3(0.0f));
	if (!keepScale) {
		SetRelativeScale(Vec3(1.0f));
	}
}

void Plu::GameObject::AttachToSkeletalMeshComponent(SkeletalMeshComponent *skeletalMeshComponent,
                                                    const String &attachPointName)
{
	AttachToComponent(skeletalMeshComponent, attachPointName, EAttachmentRule::SnapToTarget);
}

void Plu::GameObject::DetachFromSkeletalMeshComponent()
{
	DetachFromParent(EAttachmentRule::KeepWorld);
}

bool Plu::GameObject::IsAttachedToSkeletalMesh() const
{
	return GetSkeletalAttachmentParent().IsValid();
}

Plu::TUsePointer<Plu::SkeletalMeshComponent> Plu::GameObject::GetSkeletalAttachmentParent() const
{
	if (!mAttachParentComponent || mAttachSocketName.IsEmpty()) return nullptr;
	if (!mAttachParentComponent->GetClass()->IsDerivedOfOrSame(SkeletalMeshComponent::GetStaticClass())) return nullptr;
	return mObjectManager->GetObjectAsUser<SkeletalMeshComponent>(*mAttachParentComponent->GetEngineObjectHandle());
}

float Plu::GameObject::GetElapsedTimeSinceSpawn() const
{
	return mElapsedLifetime;
}

void Plu::GameObject::SetLifetime(float lifetime)
{
	if (lifetime < 0.0f) {
		PLU_CORE_ERROR("Invalid Lifetime at Setter! Must be greater than 0!");
	}
	mLifetime = lifetime;
	if (mLifetime <= mElapsedLifetime) {
		GetWorld()->DeleteGameObject(GetObjectHandle());
	}
}

Vec3 Plu::GameObject::GetObjectForwardVector() const
{
	return GetForwardVector(GetObjectRotation());
}

Vec3 Plu::GameObject::GetObjectRightVector() const
{
	return GetRightVector(GetObjectRotation());
}

Vec3 Plu::GameObject::GetObjectUpVector() const
{
	return GetUpVector(GetObjectRotation());
}

Plu::PluUUID& Plu::GameObject::GetObjectUUID()
{
	return mUuid;
}

