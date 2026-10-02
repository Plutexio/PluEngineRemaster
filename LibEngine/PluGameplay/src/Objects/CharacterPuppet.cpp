//
// Created by Plutex on 2026-06-14.
//

#include "PluEngine/Gameplay/Objects/CharacterPuppet.h"

#include "PluEngine/Gameplay/Components/CameraComponent.h"
#include "PluEngine/Gameplay/Controller.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Gameplay/Components/PhysicsBodyComponent.h"
#include "PluEngine/Gameplay/Components/PhysicsCylinderColliderComponent.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"

Plu::TUsePointer<Plu::CameraComponent> Plu::CharacterPuppet::GetCamera()
{
	return Camera;
}

bool Plu::CharacterPuppet::IsGrounded() const
{
	return mIsGrounded;
}

bool Plu::CharacterPuppet::IsSprinting() const
{
	return mIsSprinting;
}

bool Plu::CharacterPuppet::IsMoving() const
{
	return mIsMoving;
}

void Plu::CharacterPuppet::OnSetupComponents()
{
	Camera = AddComponent(CameraComponent::GetStaticClass(), "CharacterCamera");
	Camera->SetRelativeLocation(Vec3(0.f, CameraHeightOffset, 0.f));

	mCapsule = AddComponent(PhysicsCylinderColliderComponent::GetStaticClass(), "CharacterCapsule");
	ActiveBody = true; // simulate this character's body (now a per-object property)
	mCapsule->SetRadius(CapsuleRadius);
	mCapsule->SetHalfHeight(CapsuleHalfHeight);

	mBodyComponent = AddComponent(PhysicsBodyComponent::GetStaticClass(), "CharacterPhysicsBody");

	GetInputHandler()->AddActionOnHold(Key::W, [this]()
	{
		mMoveInput += GetObjectForwardVector();
		mMovingForward = true;
	});
	GetInputHandler()->AddActionOnHold(Key::S, [this](){ mMoveInput -= GetObjectForwardVector(); });
	GetInputHandler()->AddActionOnHold(Key::A, [this](){ mMoveInput -= GetObjectRightVector(); });
	GetInputHandler()->AddActionOnHold(Key::D, [this](){ mMoveInput += GetObjectRightVector(); });
	GetInputHandler()->AddActionOnHold(Key::LeftShift, [this](){ mSprinting = true; });
	GetInputHandler()->AddActionOnPress(Key::Space, [this](){ mWantsJump = true; });
}

Vec3 Plu::CharacterPuppet::GetSpawnOffset() const
{
	// The capsule is centered on the object origin, so a PlayerStart standing on the floor has to
	// lift us by a full half body height, otherwise we spawn sunk into the ground.
	return Vec3(0.f, CapsuleHalfHeight + CapsuleRadius, 0.f);
}

void Plu::CharacterPuppet::OnBeginPlay()
{
	GetController()->HideCursor();
}

bool Plu::CharacterPuppet::CheckGrounded()
{
	// How far below the bottom of the collider the floor still counts as ground.
	constexpr float kGroundTolerance = 0.1f;

	TUsePointer<SceneWorld> world = GetWorld();
	if (!world) return false;

	// The collider is a cylinder (JPH::CylinderShape) centred on the object origin, so its bottom
	// is CapsuleHalfHeight below it. The ray starts inside our own body, so the body has to be
	// filtered out — Jolt treats convex shapes as solid and would report a hit at fraction 0 on ourselves.
	const float checkDist = CapsuleHalfHeight + kGroundTolerance;
	const RaycastHitInfo hit = world->ShootRaycastInDirection(
		GetObjectLocation(), Vec3(0.f, -1.f, 0.f), checkDist, { this });
	return hit.Hit;
}

void Plu::CharacterPuppet::OnUpdate(float deltaTime)
{
	float pitch = ClampAngle(-GetInputHandler()->GetMouseDeltaY() * MouseSensitivity
		+ GetController()->GetControlRotation().x, -89.9f, 89.9f);
	Vec3 newRot = Vec3(pitch,
		GetInputHandler()->GetMouseDeltaX() * 1.f * MouseSensitivity + GetController()->GetControlRotation().y,
		0.f);
	GetController()->SetControlRotation(newRot);

	SetObjectRotation(Vec3(0.f, GetController()->GetControlRotation().y * -1.f, 0.f));
	Camera->SetRelativeRotation(Vec3(GetController()->GetControlRotation().x, 0.f, 0.f));

	mIsGrounded = CheckGrounded();

	Vec3 currentVel = mBodyComponent->GetLinearVelocity();

	Vec3 hVel = Vec3(0.f);
	mIsSprinting = false;
	mIsMoving = mMoveInput != Vec3(0.f);
	if (mIsMoving)
	{
		mIsSprinting = mSprinting && (!SprintForwardOnly || mMovingForward);
		float speed = WalkSpeed * (mIsSprinting ? SprintSpeedMultiplier : 1.f)
			* (mIsGrounded ? 1.f : AirControl);
		hVel = glm::normalize(mMoveInput) * speed;
	}

	float yVel = (mWantsJump && mIsGrounded) ? JumpForce : currentVel.y;
	mBodyComponent->SetLinearVelocity(Vec3(hVel.x, yVel, hVel.z));
	mBodyComponent->SetAngularVelocity(Vec3(0.f));

	mMoveInput = Vec3(0.f);
	mSprinting = false;
	mWantsJump = false;
	mMovingForward = false;
}
