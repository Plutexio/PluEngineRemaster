//
// Created by Plutex on 1/18/26.
//

#ifndef PLUENGINE_WORLDCOMPONENT_H
#define PLUENGINE_WORLDCOMPONENT_H
#include "GameObjectComponent.h"
#include "PluEngine/PluTypes.h"
#include "WorldComponent.generated.h"

namespace Plu
{
	class GameObject;

	// What happens to a transform when its attachment changes. Shared by component attachments
	// (WorldComponent::AttachTo) and object attachments (GameObject::AttachToComponent /
	// AttachToObject) — the same three UE-style rules apply to both.
	PLU_ENUM(PyExport, PyNamespace=Plu)
	enum class EAttachmentRule : UInt8
	{
		KeepRelative, // relative transform untouched — the attached thing snaps into the new parent's space
		KeepWorld,    // relative transform recomputed so nothing moves in the world
		SnapToTarget  // relative transform zeroed — sits exactly on the parent / socket
	};

	PLU_CLASS(PyExport, PyDerive)
	class PLUGAMEPLAY_API WorldComponent : public GameObjectComponent
	{
		REFLECTION_BODY_WORLDCOMPONENT()
	private:
		friend class GameObject;
		// Children attached to this component. Owning: a child's lifetime belongs to whatever it is
		// attached to, so reparenting moves the owning pointer between this array and either another
		// component's array or the game object's own mWorldComponents.
		DynamicArray<TOwningPointer<WorldComponent>> mWorldComponents;
		TUsePointer<WorldComponent> mParentComponent;

		// GameObjects riding this component (GameObject::AttachToComponent). Non-owning: an attached
		// object keeps its own lifetime, exactly like an attached AActor in UE — it is detached, not
		// destroyed, when this component goes away. Kept here (rather than only on the owning object)
		// so that moving a single component invalidates just the objects riding THAT component.
		DynamicArray<TUsePointer<GameObject>> mAttachedObjects;

		void Cleanup();

		// Rebuilds the relative transform so that GetWorldMatrix() reproduces worldMatrix under the
		// attachment that is current at call time. Used by the KeepWorld attachment rule.
		void SetTransformFromWorldMatrix(const Matrix4& worldMatrix);

		Vec3 mRelativeLocation = Vec3(0.0f);
		Vec3 mRelativeRotation = Vec3(0.0f);
		Vec3 mRelativeScale = Vec3(1.0f);

		Matrix4 mWorldMatrix;
		bool mRegenerateWorldMatrix = true;
		// Cache transpose(inverse(world)) — liczone leniwie w GetNormalMatrix(), unieważniane
		// razem z world matrix. inverse() 4x4 per komponent per klatka jest zbyt drogie przy
		// tysiącach komponentów (RenderSnapshotBuilder pobiera to co klatkę do InstanceGPUData).
		Matrix4 mNormalMatrix;
		bool mRegenerateNormalMatrix = true;
		// World scale (column lengths of the world matrix), refreshed in the same lazy pass as the
		// matrix itself. Recomputing it per call meant three glm::length plus a 64-byte matrix copy
		// for every caller — the snapshot builder asks every component for it every frame.
		Vec3 mWorldScale = Vec3(1.0f);
		// Incremented on every invalidation of the world matrix. Anything that derives world-space
		// data from this transform (world bounds, per-instance matrix caches) can then tell
		// "unchanged since I last looked" from an integer compare instead of re-deriving the value
		// or comparing whole matrices. Starts at 1, so 0 is free as a "never computed" marker.
		UInt32 mTransformVersion = 1;
		void MarkWorldMatrixForRegeneration();
		// translate(loc) * rotate(rot) * scale(scale) — this component's own transform, relative to
		// whatever it is attached to.
		Matrix4 BuildLocalMatrix();
	protected:
		// Called after any of the relative transform setters changed a value. Components that
		// contribute collision geometry override this to keep the physics body in sync — sub-shape
		// offsets are baked into the compound shape when the body is built, so a moved component is
		// invisible to physics until the body is rebuilt.
		virtual void OnRelativeTransformChanged() {}
		// Queues a rebuild of the owning object's physics body. No-op outside of play, or when the
		// object has no body yet.
		void MarkOwnerCollisionDirty();
	public:
		WorldComponent() = default;
		virtual ~WorldComponent() override = default;

		// The component this one is attached to, or null when it hangs directly off the game object.
		PLU_FUNCTION()
		[[nodiscard]] TUsePointer<WorldComponent> GetParentComponent() const;
		PLU_FUNCTION()
		DynamicArray<TUsePointer<WorldComponent>> GetChildren();

		// Attaches this component under newAttachPoint, or directly under the owning game object when
		// newAttachPoint is null. Both components must belong to the same game object — parenting
		// across objects is a GameObject-level concept (see GameObject::AttachToComponent).
		// Rejected (with an error log, leaving the attachment untouched): attaching to itself, or to
		// one of its own descendants, which would build a transform cycle.
		PLU_FUNCTION()
		void AttachTo(WorldComponent* newAttachPoint, EAttachmentRule rule = EAttachmentRule::KeepRelative);
		// Shorthand for AttachTo(nullptr, rule) — moves the component back under the game object.
		PLU_FUNCTION()
		void Detach(EAttachmentRule rule = EAttachmentRule::KeepRelative);
		// Drops the offset from whatever this component hangs off — its attach point, or the owning
		// object's origin when it has none. keepScale leaves the relative scale alone. Mirrors
		// GameObject::SnapToAttachParent.
		PLU_FUNCTION()
		void SnapToAttachParent(bool keepScale = false);
		// True when component is this component's parent, grandparent, … Walking up is cheap (chains
		// are a handful of levels deep) and is what guards AttachTo against cycles.
		PLU_FUNCTION()
		[[nodiscard]] bool IsAttachedTo(WorldComponent* component) const;

		// GameObjects riding this component (GameObject::AttachToComponent), one level deep.
		PLU_FUNCTION()
		[[nodiscard]] DynamicArray<TUsePointer<GameObject>> GetAttachedObjects() const;

		Matrix4 GetWorldMatrix();
		Matrix4 GetNormalMatrix();

		// The same two matrices without the 64-byte copy, for per-frame readers. RenderSnapshotBuilder
		// walks every component of the scene each frame, and the copies alone were measurable on a
		// scene with a couple of thousand components.
		const Matrix4& GetWorldMatrixRef();
		const Matrix4& GetNormalMatrixRef();

		// Changes whenever this component's world matrix is invalidated (its own transform, an
		// ancestor's, or a reparent). Cheap staleness check for caches built on top of the transform
		// — see StaticMeshComponent::GetWorldBoundingSphere.
		[[nodiscard]] UInt32 GetTransformVersion() const { return mTransformVersion; }

		// The full chain of relative transforms up to (but excluding) the owning game object, i.e.
		// this component's transform in object space. Differs from BuildLocalMatrix() only for
		// components attached to another component via AttachTo, whose relative transform is
		// parent-component-relative. Not cached — used at physics body build time, not per frame.
		Matrix4 GetMatrixRelativeToGameObject();

		PLU_FUNCTION()
		Vec3 GetRelativeLocation();
		PLU_FUNCTION()
		Vec3 GetRelativeRotation();
		PLU_FUNCTION()
		Vec3 GetRelativeScale();

		PLU_FUNCTION()
		void SetRelativeLocation(Vec3 newLoc);
		PLU_FUNCTION()
		void SetRelativeRotation(Vec3 newRot);
		PLU_FUNCTION()
		void SetRelativeScale(Vec3 newScale);

		PLU_FUNCTION()
		Vec3 GetWorldLocation();
		PLU_FUNCTION()
		Vec3 GetWorldRotation();
		PLU_FUNCTION()
		Vec3 GetWorldScale();

		// World-space unit axes of this component, read straight from its world matrix (so they
		// include every parent's rotation). Same convention as GetForwardVector: forward is -Z.
		PLU_FUNCTION()
		Vec3 GetWorldForwardVector();
		PLU_FUNCTION()
		Vec3 GetWorldRightVector();
		PLU_FUNCTION()
		Vec3 GetWorldUpVector();

		PLU_FUNCTION()
		void SetWorldLocation(Vec3 newLoc);
		PLU_FUNCTION()
		void SetWorldRotation(Vec3 newRot);
		PLU_FUNCTION()
		void SetWorldScale(Vec3 newScale);

		JSON Serialize();
	};
}

#endif //PLUENGINE_WORLDCOMPONENT_H
