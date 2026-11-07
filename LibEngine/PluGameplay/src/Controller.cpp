//
// Created by Plutex on 2026-02-14.
//

#include "PluEngine/Gameplay/Controller.h"

#include "PluEngine/Gameplay/GameClient.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Gameplay/Components/CameraComponent.h"
#include "PluEngine/Gameplay/GameClient.h"
#include "PluEngine/Gameplay/Puppet.h"
#include "PluEngine/Gameplay/Scenes/ScenesManager.h"
#include "PluEngine/Gameplay/Scenes/SceneWorld.h"

Plu::InputHandler * Plu::Controller::GetInputHandler()
{
	return &mControllerInputHandler;
}

void Plu::Controller::SetControlRotation(Vec3 newRot)
{
	NormalizeVec3Rotation(&newRot);
	mControlRotation = newRot;
	Vec3 newPoint = GetSphericalOrbitPoint(Vec3(0), 5, newRot.y, newRot.x);
	mRealControlRotation = GetLookAtRotatorDegrees(Vec3(0), newPoint);
	mRealControlRotation = GetForwardVector(mRealControlRotation);
}

Vec3 Plu::Controller::GetControlRotation() const
{
	return mControlRotation;
}

Vec3 Plu::Controller::GetControlRotationForPuppet() const
{
	return mRealControlRotation;
}

Plu::Controller::~Controller()
{
	Unpossess();
}

void Plu::Controller::Possess(TUsePointer<Puppet> puppet)
{
	PLU_ASSERT(puppet, "Puppet is invalid on Possess!")
	mPossessedPuppet = puppet;
	puppet->mController = This();
	puppet->OnPossessed(This());
	mPuppetCamera = puppet->GetComponentByClass(CameraComponent::GetStaticClass());
}

void Plu::Controller::Unpossess()
{
	if (!mPossessedPuppet) return;
	mPossessedPuppet->OnUnpossessed();
	mPossessedPuppet = nullptr;
}

bool Plu::Controller::IsCursorShown() const
{
	return GetGameClient()->IsCursorShown();
}

void Plu::Controller::HideCursor()
{
	GetGameClient()->HideCursor();
}

void Plu::Controller::ShowCursor()
{
	GetGameClient()->ShowCursor();
}

Plu::TUsePointer<Plu::Puppet> Plu::Controller::GetControllerPuppet()
{
	return mPossessedPuppet;
}

Plu::TUsePointer<Plu::CameraComponent> Plu::Controller::GetControlledPuppetCamera()
{
	if (!mPuppetCamera.IsValid()) {
		mPuppetCamera = nullptr;
	}
	return mPuppetCamera;
}
