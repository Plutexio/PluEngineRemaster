//
// Created by Plutex on 2026-03-07.
//

#ifndef PLUENGINE_PHYSICSBODY_H
#define PLUENGINE_PHYSICSBODY_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "PluEngine/Core/Objects/EngineObject.h"
#include "PhysicsBody.generated.h"
#include "PluEngine/Gameplay/Components/PhysicsBodyComponent.h"
#include "PluEngine/Gameplay/Physics/PhysicsChannels.h"

namespace Plu
{
	PLU_CLASS(Abstract)
	class PLUPHYSICS_API PhysicsBody : public EngineObject
	{
		REFLECTION_BODY_PHYSICSBODY()
	public:
		// DeferAdd creates the body without inserting it into the physics system. Jolt's own warning
		// on AddBody is that adding many bodies one at a time leaves the broadphase degenerate until
		// it is rebuilt, so bulk paths (a scene starting play) create every body deferred and hand
		// them to AddBodiesPrepare/AddBodiesFinalize in one go — see PhysicsWorld::FlushPendingBodies.
		// A deferred body is inert until added: it has an ID but takes part in nothing.
		PhysicsBody(
			JPH::BodyInterface& BodyInterface,
			JPH::ShapeRefC      Shape,
			const JPH::RVec3&   Position,
			const JPH::Quat&    Rotation    = JPH::Quat::sIdentity(),
			PhysicsBodyType            Type        = PhysicsBodyType::Static,
			PhysicsCollisionChannel* Channel = PhysicsChannelsManager::GetInstance()->GetChannel("Default"),
			float               Friction    = 0.2f,
			float               Restitution = 0.0f,
			float				Mass         = 10.0f,
			bool                DeferAdd     = false
		);

		// Whether this body wants EActivation::Activate when it is added (non-static bodies do).
		// The batch add takes one activation mode per batch, so the caller partitions on this.
		[[nodiscard]] bool NeedsActivation() const { return mNeedsActivation; }

		PhysicsBodyType GetBodyType() const { return mBodyType; }

		void SetCollisionChannel(PhysicsCollisionChannel* Channel) const;

		// Whether the body was still active at the previous transform sync (PhysicsWorld::OnUpdate).
		// Jolt deactivates a body at the end of the step that brought it to rest, so the sync needs
		// one more pass after that to hand the final transform to the GameObject.
		bool WasActiveOnLastSync = false;
		~PhysicsBody();

		PhysicsBody(const PhysicsBody&) = delete;
		PhysicsBody& operator=(const PhysicsBody&) = delete;

		[[nodiscard]] JPH::RVec3  GetPosition() const;
		JPH::Quat   GetRotation() const;
		[[nodiscard]] JPH::BodyID GetID()       const { return mBodyID; }
		[[nodiscard]] bool        IsValid()     const { return !mBodyID.IsInvalid(); }

		void SetPosition(const JPH::RVec3& Position);
		void SetRotation(const JPH::Quat&  Rotation);

		JPH::Vec3 GetLinearVelocity() const;
		void SetLinearVelocity(const JPH::Vec3& Velocity);
		void AddLinearVelocity(const JPH::Vec3& Velocity);

		JPH::Vec3 GetAngularVelocity() const;
		void SetAngularVelocity(const JPH::Vec3& AngularVelocity);

		float GetFriction() const;
		void  SetFriction(float Friction);

		float GetRestitution() const;
		void  SetRestitution(float Restitution);

		void AddForce(const JPH::Vec3&         Force);
		void AddTorque(const JPH::Vec3&         Torque);
		void AddImpulse(const JPH::Vec3&        Impulse);
		void AddAngularImpulse(const JPH::Vec3& Impulse);

	private:
		JPH::BodyInterface& mBodyInterface;
		JPH::BodyID         mBodyID;
		bool                mNeedsActivation = false;

		PhysicsBodyType mBodyType;

		static JPH::EMotionType ToJoltMotionType(PhysicsBodyType Type);
		static JPH::ObjectLayer ToJoltLayer(PhysicsCollisionChannel* channel, PhysicsBodyType Type);
	};
}

#endif //PLUENGINE_PHYSICSBODY_H