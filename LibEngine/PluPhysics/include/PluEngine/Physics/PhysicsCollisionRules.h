//
// Created by Plutex on 2026-03-07.
//

#ifndef PLUENGINE_PHYSICSCOLLISIONRULES_H
#define PLUENGINE_PHYSICSCOLLISIONRULES_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>

#include "PluEngine/Gameplay/Physics/PhysicsChannels.h"

namespace Plu
{

	class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
	public:
		bool ShouldCollide(JPH::ObjectLayer A, JPH::ObjectLayer B) const override {
			return !PhysicsChannelsManager::GetInstance()->CanBeCompletelyIgnored(A,B);
		}
	};

	namespace BroadPhaseLayers {
		static constexpr JPH::BroadPhaseLayer STATIC(0);
		static constexpr JPH::BroadPhaseLayer DYNAMIC(1);
	}

	class BPLayerInterfaceImpl : public JPH::BroadPhaseLayerInterface {
	public:
		[[nodiscard]] unsigned int GetNumBroadPhaseLayers() const override { return 2; }

		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
			return (layer & 1) ? BroadPhaseLayers::DYNAMIC : BroadPhaseLayers::STATIC;
		}
	};

	class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
	public:
		bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override {
			if (layer & 1) {
				return true;
			} else {
				return bp == BroadPhaseLayers::DYNAMIC;
			}
		}
	};

	class PluContactListener : public JPH::ContactListener
	{
	public:
		void OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) override;
		void OnContactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) override;
		void OnContactRemoved(const JPH::SubShapeIDPair &inSubShapePair) override;
	};
}

#endif //PLUENGINE_PHYSICSCOLLISIONRULES_H