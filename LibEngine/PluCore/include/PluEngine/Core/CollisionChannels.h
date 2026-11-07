//
// Created by Plutex on 2026-06-29.
//
// UE-style collision channels. A body is assigned a *collision profile* (the UE
// "Collision Preset" concept): the channel the body IS (ObjectType) plus a per-channel
// response (Ignore / Overlap / Block). The combined response for a body pair is the
// weaker of the two directional responses (Ignore < Overlap < Block). Filtering itself
// is applied in PhysicsCollisionRules (ChannelGroupFilter) and PhysicsWorld's contact
// listener; this header only owns the data model + helpers.
//

#ifndef PLUENGINE_COLLISIONCHANNELS_H
#define PLUENGINE_COLLISIONCHANNELS_H

#include "CollisionChannels.generated.h"
#include "PluEngine/Core.h"
#include "PluSTL/PluSTL_FWD.h"
#include "PluEngine/PluTypes.h"

namespace Plu
{
	// Permissiveness order matters: Ignore < Overlap < Block. CombineResponse relies on it.
	PLU_ENUM(PyExport,PyNamespace=Plu)
	enum class CollisionResponse : UInt8
	{
		Ignore  = 0, // no contact at all (rejected in ChannelGroupFilter::CanCollide)
		Overlap = 1, // contact reported as event, no physical blocking (ContactSettings::mIsSensor)
		Block   = 2  // full physical contact
	};

	// One UE-style collision preset. ResponseTo is indexed by ObjectType (channel index)
	// and is sized to the number of channels in the owning CollisionConfig.
	struct PLUCORE_API CollisionProfile
	{
		String                          Name;
		UInt8                           ObjectType = 0;        // index into CollisionConfig::ChannelNames
		DynamicArray<CollisionResponse> ResponseTo;           // response towards each channel
	};

	// A reference to a collision profile *by name*. A distinct type (not a bare String) so the
	// editor renders a preset dropdown via TypeSerializer<CollisionProfileRef>; serialization
	// stores just the name. Used as a PhysicsBodyComponent property.
	struct PLUCORE_API CollisionProfileRef
	{
		String Name = "Default";
	};

	// Project-level, data-driven collision configuration (edited in Project Settings,
	// saved with the project, loaded by both editor and runtime).
	struct PLUCORE_API CollisionConfig
	{
		DynamicArray<String>           ChannelNames;          // index == ObjectType id
		DynamicArray<CollisionProfile> Profiles;              // index == profile id stored in body GroupID

		// Resolve a profile name to its index; returns 0 (first profile) when not found.
		[[nodiscard]] UInt32 FindProfileIndex(const String& name) const;
		// Ensure every profile's ResponseTo array matches ChannelNames size (pads with Block).
		void NormalizeProfiles();
	};

	// Weaker of two responses (Ignore beats Overlap beats Block).
	inline CollisionResponse CombineResponse(CollisionResponse a, CollisionResponse b)
	{
		return (static_cast<UInt8>(a) < static_cast<UInt8>(b)) ? a : b;
	}

	// Combined response for a pair of bodies given their profile indices (as stored in
	// the Jolt CollisionGroup GroupID). Out-of-range indices resolve to Block (safe default).
	PLUCORE_API CollisionResponse ResolvePairResponse(const CollisionConfig& config, UInt32 profileA, UInt32 profileB);

	// Built-in default config mirroring the common UE channels/presets.
	PLUCORE_API CollisionConfig BuildDefaultCollisionConfig();

	// JSON (de)serialization. Format is a self-contained object: { channels:[...], profiles:[...] }.
	PLUCORE_API JSON            SaveCollisionConfig(const CollisionConfig& config);
	PLUCORE_API CollisionConfig LoadCollisionConfig(const JSON& json);

	// Process-wide active collision config = the loaded project's config. PhysicsWorld reads
	// it live (so Project Settings edits take effect on the next body rebuild). The editor /
	// runtime assign it on project load; the Project Settings UI mutates it in place.
	// Defaults to BuildDefaultCollisionConfig() until a project sets it.
	PLUCORE_API CollisionConfig& ActiveCollisionConfig();
}

#endif //PLUENGINE_COLLISIONCHANNELS_H
