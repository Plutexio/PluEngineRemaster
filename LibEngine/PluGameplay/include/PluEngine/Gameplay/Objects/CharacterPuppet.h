//
// Created by Plutex on 2026-06-14.
//

#ifndef PLUENGINE_CHARACTERPUPPET_H
#define PLUENGINE_CHARACTERPUPPET_H
#include "PluEngine/Core.h"
#include "PluEngine/Gameplay/Puppet.h"
#include "CharacterPuppet.generated.h"

namespace Plu
{
	class PhysicsBodyComponent;
	class PhysicsCylinderColliderComponent;
	class CameraComponent;

	PLU_CLASS(PyExport, PyDerive)
	class PLUGAMEPLAY_API CharacterPuppet : public Puppet
	{
		REFLECTION_BODY_CHARACTERPUPPET()
	private:
		Vec3 mMoveInput;
		bool mSprinting = false;
		bool mWantsJump = false;
		bool mIsGrounded = false;
		bool mIsSprinting = false;
		bool mIsMoving = false;
		bool mMovingForward = false;

		TUsePointer<PhysicsCylinderColliderComponent> mCapsule;
		TUsePointer<PhysicsBodyComponent> mBodyComponent;

		bool CheckGrounded();

	public:
		CharacterPuppet() = default;
		~CharacterPuppet() override = default;

		PLU_PROPERTY(PyExport)
		float WalkSpeed = 5.f;

		PLU_PROPERTY(PyExport)
		float SprintSpeed = 8.f;

		PLU_PROPERTY(PyExport)
		float JumpForce = 8.f;

		PLU_PROPERTY(PyExport)
		float MouseSensitivity = 1.f;

		PLU_PROPERTY(PyExport)
		float CapsuleRadius = 0.4f;

		PLU_PROPERTY(PyExport)
		float CapsuleHalfHeight = 0.9f;

		PLU_PROPERTY(PyExport)
		float CameraHeightOffset = 0.75f;

		PLU_PROPERTY(PyExport)
		float AirControl = 0.3f;

		PLU_PROPERTY(PyExport)
		bool SprintForwardOnly = false;

		TUsePointer<CameraComponent> Camera;

		PLU_FUNCTION(PyExport)
		TUsePointer<CameraComponent> GetCamera();

		PLU_FUNCTION(PyExport, PyOverride)
		virtual bool CanSprint();

		// Cached result of the last CheckGrounded() done in OnUpdate().
		PLU_FUNCTION(PyExport)
		bool IsGrounded() const;

		// True only while the sprint speed multiplier is actually being applied — the character has
		// to be moving and, with SprintForwardOnly, moving forward.
		PLU_FUNCTION(PyExport)
		bool IsSprinting() const;

		// True while there was movement input this frame — driven by the input, not by the body's
		// actual velocity, so sliding or falling on its own does not count as moving.
		PLU_FUNCTION(PyExport)
		bool IsMoving() const;

		Vec3 GetSpawnOffset() const override;

		void OnSetupComponents() override;
		void OnBeginPlay() override;
		void OnUpdate(float deltaTime) override;
	};
}

#endif //PLUENGINE_CHARACTERPUPPET_H
