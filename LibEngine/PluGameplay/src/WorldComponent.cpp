//
// Created by Plutex on 1/18/26.
//

#include "PluEngine/Gameplay/WorldComponent.h"

#include <glm/gtc/matrix_inverse.hpp>

#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Core/Objects/EngineObjectManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"

void Plu::WorldComponent::Cleanup()
{
	// Objects riding this component outlive it — hand them back to the world before it goes away,
	// keeping them where they are (see GameObject::Cleanup for the same rule on the object side).
	DynamicArray<TUsePointer<GameObject>> attachedObjects = mAttachedObjects;
	for (const auto& attached : attachedObjects) {
		if (!attached) continue;
		attached->DetachFromParent(EAttachmentRule::KeepWorld);
	}
	for (const auto& component : mWorldComponents) {
		component->Cleanup();
		GetObjectManagerFromParent()->DestroyObject(*component->GetEngineObjectHandle());
	}
	mWorldComponents.Clear();
}

void Plu::WorldComponent::MarkWorldMatrixForRegeneration()
{
	mRegenerateWorldMatrix = true;
	mRegenerateNormalMatrix = true;
	// 0 stays reserved for "never computed" on the caches keyed by this counter, so it is skipped
	// on the (theoretical) wrap.
	if (++mTransformVersion == 0) mTransformVersion = 1;
	for (auto child : mWorldComponents) {
		child->MarkWorldMatrixForRegeneration();
	}
	// GameObjects riding this component derive their world transform from it, and so does everything
	// hanging off them — without this they would keep serving a stale matrix until something else
	// happened to invalidate it.
	for (const auto& attached : mAttachedObjects) {
		if (!attached) continue;
		attached->MarkWorldMatrixForRegeneration();
	}
}

DynamicArray<Plu::TUsePointer<Plu::GameObject>> Plu::WorldComponent::GetAttachedObjects() const
{
	return mAttachedObjects;
}

void Plu::WorldComponent::MarkOwnerCollisionDirty()
{
	TUsePointer<SceneWorld> world = GetWorld();
	if (!world) return;
	world->OnComponentTransformChanged(GetParentGameObject().GetRaw());
}

Plu::TUsePointer<Plu::WorldComponent> Plu::WorldComponent::GetParentComponent() const
{
	return mParentComponent;
}

DynamicArray<Plu::TUsePointer<Plu::WorldComponent>> Plu::WorldComponent::GetChildren()
{
	DynamicArray<TUsePointer<WorldComponent>> children(mWorldComponents.Size());
	for (const auto& child : mWorldComponents) {
		children.PushBack(child);
	}
	return children;
}

bool Plu::WorldComponent::IsAttachedTo(WorldComponent *component) const
{
	if (!component) return false;
	const WorldComponent* current = mParentComponent.GetRaw();
	while (current) {
		if (current == component) return true;
		current = current->mParentComponent.GetRaw();
	}
	return false;
}

void Plu::WorldComponent::AttachTo(WorldComponent *newAttachPoint, EAttachmentRule rule)
{
	TUsePointer<GameObject> owner = GetParentGameObject();
	if (!owner) {
		PLU_CORE_ERROR("AttachTo on a component that has no owning GameObject yet — register it with GameObject::AddComponent first.");
		return;
	}
	if (newAttachPoint == this) {
		PLU_CORE_ERROR("Cannot attach component '{}' to itself.", GetComponentName().CStr());
		return;
	}
	if (newAttachPoint && newAttachPoint->GetParentGameObject().GetRaw() != owner.GetRaw()) {
		PLU_CORE_ERROR("Cannot attach component '{}' to '{}' — they belong to different GameObjects.",
			GetComponentName().CStr(), newAttachPoint->GetComponentName().CStr());
		return;
	}
	// A parent attached below its own child would make GetWorldMatrix() recurse forever.
	if (newAttachPoint && newAttachPoint->IsAttachedTo(this)) {
		PLU_CORE_ERROR("Cannot attach component '{}' to its own descendant '{}' — that would form a cycle.",
			GetComponentName().CStr(), newAttachPoint->GetComponentName().CStr());
		return;
	}
	if (mParentComponent.GetRaw() == newAttachPoint) return;

	// Snapshot before the reparent: GetWorldMatrix() answers against the OLD chain here.
	const Matrix4 previousWorldMatrix = (rule == EAttachmentRule::KeepWorld) ? GetWorldMatrix() : Matrix4(1.0f);

	// Owning reference held across the whole swap — the old list drops its owning pointer below, and
	// without this the component would be destroyed mid-call.
	TOwningPointer<WorldComponent> self = GetObjectManagerFromParent()->GetObjectAsOwner<WorldComponent>(*GetEngineObjectHandle());

	owner->OnDetachComponent(self, mParentComponent);
	mParentComponent = newAttachPoint
		? GetObjectManagerFromParent()->GetObjectAsUser<WorldComponent>(*newAttachPoint->GetEngineObjectHandle())
		: nullptr;
	owner->OnAttachComponent(self, mParentComponent);

	MarkWorldMatrixForRegeneration();
	if (rule == EAttachmentRule::KeepWorld) {
		SetTransformFromWorldMatrix(previousWorldMatrix);
	}
	// The component's transform relative to the game object changed even under KeepWorld (the chain
	// it is baked from is different), and sub-shape offsets are baked at body build time.
	OnRelativeTransformChanged();
	MarkOwnerCollisionDirty();
}

void Plu::WorldComponent::Detach(EAttachmentRule rule)
{
	AttachTo(nullptr, rule);
}

void Plu::WorldComponent::SnapToAttachParent(bool keepScale)
{
	// No IsAttached() guard, unlike the GameObject version: a component always hangs off something —
	// another component, or the owning object, whose origin it then snaps to.
	SetRelativeLocation(Vec3(0.0f));
	SetRelativeRotation(Vec3(0.0f));
	if (!keepScale) {
		SetRelativeScale(Vec3(1.0f));
	}
}

void Plu::WorldComponent::SetTransformFromWorldMatrix(const Matrix4 &worldMatrix)
{
	const Matrix4 parentWorld = mParentComponent
		? mParentComponent->GetWorldMatrix()
		: GetParentGameObject()->GetObjectWorldMatrix();
	const Matrix4 localMatrix = glm::inverse(parentWorld) * worldMatrix;

	// Set the fields directly instead of going through the three setters: each of them marks the
	// whole subtree for regeneration and fires OnRelativeTransformChanged, and the caller does both
	// once for all three values.
	mRelativeLocation = GetLocationFromMatrix(localMatrix);
	mRelativeRotation = GetRotationFromMatrix(localMatrix);
	mRelativeScale = GetScaleFromMatrix(localMatrix);
	MarkWorldMatrixForRegeneration();
}

Matrix4 Plu::WorldComponent::BuildLocalMatrix()
{
	return glm::translate(glm::mat4(1.0f), GetRelativeLocation()) *
		  glm::mat4_cast(glm::quat(glm::radians(GetRelativeRotation()))) *
		  glm::scale(glm::mat4(1.0f), GetRelativeScale());
}

const Matrix4& Plu::WorldComponent::GetWorldMatrixRef()
{
	if (mRegenerateWorldMatrix) {
		mRegenerateWorldMatrix = false;
		Matrix4 localMatrix = BuildLocalMatrix();
		// parent * local in both branches — the local transform is expressed in the parent's space,
		// so it has to be applied first (glm is column-vector, rightmost factor applies first).
		if (mParentComponent) {
			mWorldMatrix = mParentComponent->GetWorldMatrixRef() * localMatrix;
		} else {
			mWorldMatrix = GetParentGameObject()->GetObjectWorldMatrix() * localMatrix;
		}
		// Falls out of the same pass rather than being derived again on every GetWorldScale() call.
		mWorldScale = Vec3(
			glm::length(Vec3(mWorldMatrix[0])),
			glm::length(Vec3(mWorldMatrix[1])),
			glm::length(Vec3(mWorldMatrix[2]))
		);
	}
	return mWorldMatrix;
}

Matrix4 Plu::WorldComponent::GetWorldMatrix()
{
	return GetWorldMatrixRef();
}

Matrix4 Plu::WorldComponent::GetMatrixRelativeToGameObject()
{
	Matrix4 localMatrix = BuildLocalMatrix();
	if (mParentComponent) {
		return mParentComponent->GetMatrixRelativeToGameObject() * localMatrix;
	}
	return localMatrix;
}

const Matrix4& Plu::WorldComponent::GetNormalMatrixRef()
{
	if (mRegenerateNormalMatrix) {
		mRegenerateNormalMatrix = false;
		// Every consumer reads mat3(normalMatrix) — the instanced and the plain vertex shader alike
		// — and for an affine world matrix that 3x3 block is exactly the inverse-transpose of the
		// matrix's own 3x3 block. Inverting the 3x3 gives the same result for a fraction of the work.
		mNormalMatrix = Matrix4(glm::inverseTranspose(glm::mat3(GetWorldMatrixRef())));
	}
	return mNormalMatrix;
}

Matrix4 Plu::WorldComponent::GetNormalMatrix()
{
	return GetNormalMatrixRef();
}

Vec3 Plu::WorldComponent::GetRelativeLocation()
{
	return mRelativeLocation;
}

Vec3 Plu::WorldComponent::GetRelativeRotation()
{
	return mRelativeRotation;
}

Vec3 Plu::WorldComponent::GetRelativeScale()
{
	return mRelativeScale;
}

void Plu::WorldComponent::SetRelativeLocation(Vec3 newLoc)
{
	mRelativeLocation = newLoc;
	MarkWorldMatrixForRegeneration();
	OnRelativeTransformChanged();
	DispatchEvent("RelativeLocationChanged", nullptr);
}

void Plu::WorldComponent::SetRelativeRotation(Vec3 newRot)
{
	mRelativeRotation = newRot;
	MarkWorldMatrixForRegeneration();
	OnRelativeTransformChanged();
	DispatchEvent("RelativeRotationChanged", nullptr);
}

void Plu::WorldComponent::SetRelativeScale(Vec3 newScale)
{
	mRelativeScale = newScale;
	MarkWorldMatrixForRegeneration();
	OnRelativeTransformChanged();
	DispatchEvent("RelativeScaleChanged", nullptr);
}

Vec3 Plu::WorldComponent::GetWorldLocation()
{
	return Vec3(GetWorldMatrixRef()[3]);
}

Vec3 Plu::WorldComponent::GetWorldScale()
{
	// Derived by the world-matrix refresh; this call only makes sure that pass has run.
	GetWorldMatrixRef();
	return mWorldScale;
}

Vec3 Plu::WorldComponent::GetWorldRotation()
{
	const Matrix4& m = GetWorldMatrixRef();
	Vec3 scale = GetWorldScale();
	glm::mat3 rotMat = glm::mat3(
		Vec3(m[0]) / scale.x,
		Vec3(m[1]) / scale.y,
		Vec3(m[2]) / scale.z
	);
	return glm::degrees(glm::eulerAngles(glm::quat_cast(rotMat)));
}

Vec3 Plu::WorldComponent::GetWorldForwardVector()
{
	return glm::normalize(-Vec3(GetWorldMatrixRef()[2]));
}

Vec3 Plu::WorldComponent::GetWorldRightVector()
{
	return glm::normalize(Vec3(GetWorldMatrixRef()[0]));
}

Vec3 Plu::WorldComponent::GetWorldUpVector()
{
	return glm::normalize(Vec3(GetWorldMatrixRef()[1]));
}

void Plu::WorldComponent::SetWorldLocation(Vec3 newLoc)
{
	Matrix4 parentWorld = (mParentComponent != nullptr)
		? mParentComponent->GetWorldMatrix()
		: GetParentGameObject()->GetObjectWorldMatrix();

	Vec3 localPos = Vec3(glm::inverse(parentWorld) * Vec4(newLoc, 1.0f));
	SetRelativeLocation(localPos);
}

void Plu::WorldComponent::SetWorldRotation(Vec3 newRot)
{
	Quaternion worldRot = Quaternion(glm::radians(newRot));
	Quaternion parentWorldRot;

	if (mParentComponent != nullptr)
		parentWorldRot = Quaternion(glm::radians(mParentComponent->GetWorldRotation()));
	else
		parentWorldRot = Quaternion(glm::radians(GetParentGameObject()->GetObjectRotation()));

	Quaternion localRot = glm::inverse(parentWorldRot) * worldRot;
	SetRelativeRotation(glm::degrees(glm::eulerAngles(localRot)));
}

void Plu::WorldComponent::SetWorldScale(Vec3 newScale)
{
	Vec3 parentScale = (mParentComponent != nullptr)
		? mParentComponent->GetWorldScale()
		: GetParentGameObject()->GetObjectScale();

	SetRelativeScale(newScale / parentScale);
}

JSON Plu::WorldComponent::Serialize()
{
	JSON j = TypeSerializer<TypeInfo*>::Serialize(this->GetClass(), this);
	j["children"] = JSON::array();
	for (const auto& child : mWorldComponents) {
		j["children"].push_back(child->Serialize());
	}
	Vec3 relativeLocation = GetRelativeLocation();
	Vec3 relativeRotation = GetRelativeRotation();
	Vec3 relativeScale = GetRelativeScale();
	j["relativeLocation"] = TypeSerializer<Vec3>::Serialize(&relativeLocation);
	j["relativeRotation"] = TypeSerializer<Vec3>::Serialize(&relativeRotation);
	j["relativeScale"] = TypeSerializer<Vec3>::Serialize(&relativeScale);
	return j;
}
