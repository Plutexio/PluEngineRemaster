//
// Created by Plutex on 2026-06-29.
//

#include "PluEngine/Core/CollisionChannels.h"

#include "PluSTL/String/String.h"
#include "PluSTL/Array/Array.h"
#include "json.hpp"

using namespace Plu;

UInt32 CollisionConfig::FindProfileIndex(const String& name) const
{
	for (UInt32 i = 0; i < Profiles.Size(); ++i)
		if (Profiles[i].Name == name)
			return i;
	return 0;
}

void CollisionConfig::NormalizeProfiles()
{
	const UInt32 channelCount = ChannelNames.Size();
	for (UInt32 p = 0; p < Profiles.Size(); ++p)
	{
		auto& resp = Profiles[p].ResponseTo;
		// Default any missing channel response to Block (most conservative).
		while (resp.Size() < channelCount)
			resp.PushBack(CollisionResponse::Block);
		if (Profiles[p].ObjectType >= channelCount)
			Profiles[p].ObjectType = 0;
	}
}

CollisionResponse Plu::ResolvePairResponse(const CollisionConfig& config, UInt32 profileA, UInt32 profileB)
{
	if (profileA >= config.Profiles.Size() || profileB >= config.Profiles.Size())
		return CollisionResponse::Block;

	const CollisionProfile& pa = config.Profiles[profileA];
	const CollisionProfile& pb = config.Profiles[profileB];

	// Guard against profiles that haven't been normalized to the current channel count.
	if (pb.ObjectType >= pa.ResponseTo.Size() || pa.ObjectType >= pb.ResponseTo.Size())
		return CollisionResponse::Block;

	const CollisionResponse respA = pa.ResponseTo[pb.ObjectType];
	const CollisionResponse respB = pb.ResponseTo[pa.ObjectType];
	return CombineResponse(respA, respB);
}

CollisionConfig Plu::BuildDefaultCollisionConfig()
{
	CollisionConfig cfg;

	// Channels (object types) — mirrors the common UE set.
	const char* channels[] = { "WorldStatic", "WorldDynamic", "Pawn", "PhysicsBody", "Trigger" };
	for (const char* c : channels)
		cfg.ChannelNames.PushBack(String(c));

	const UInt32 n = cfg.ChannelNames.Size();
	auto makeProfile = [n](const char* name, UInt8 objectType, CollisionResponse fill) {
		CollisionProfile p;
		p.Name = String(name);
		p.ObjectType = objectType;
		for (UInt32 i = 0; i < n; ++i)
			p.ResponseTo.PushBack(fill);
		return p;
	};

	// Index 0 is the fallback profile (used when a name can't be resolved) -> Default = BlockAll.
	CollisionProfile def = makeProfile("Default", /*WorldDynamic*/1, CollisionResponse::Block);
	cfg.Profiles.PushBack(def);

	cfg.Profiles.PushBack(makeProfile("WorldStatic",  0, CollisionResponse::Block));
	cfg.Profiles.PushBack(makeProfile("BlockAll",     1, CollisionResponse::Block));

	// Pawn: blocks world + other pawns, overlaps triggers.
	{
		CollisionProfile pawn = makeProfile("Pawn", 2, CollisionResponse::Block);
		pawn.ResponseTo[4] = CollisionResponse::Overlap; // Trigger channel
		cfg.Profiles.PushBack(pawn);
	}

	// Trigger: overlaps everything, blocks nothing.
	cfg.Profiles.PushBack(makeProfile("Trigger",    4, CollisionResponse::Overlap));
	cfg.Profiles.PushBack(makeProfile("OverlapAll", 1, CollisionResponse::Overlap));
	cfg.Profiles.PushBack(makeProfile("NoCollision",1, CollisionResponse::Ignore));

	cfg.NormalizeProfiles();
	return cfg;
}

JSON Plu::SaveCollisionConfig(const CollisionConfig& config)
{
	JSON j;
	j["channels"] = JSON::array();
	for (UInt32 i = 0; i < config.ChannelNames.Size(); ++i)
		j["channels"].push_back(config.ChannelNames[i].CStr());

	j["profiles"] = JSON::array();
	for (UInt32 p = 0; p < config.Profiles.Size(); ++p)
	{
		const CollisionProfile& prof = config.Profiles[p];
		JSON jp;
		jp["name"] = prof.Name.CStr();
		jp["objectType"] = prof.ObjectType;
		jp["responses"] = JSON::array();
		for (UInt32 i = 0; i < prof.ResponseTo.Size(); ++i)
			jp["responses"].push_back(static_cast<UInt8>(prof.ResponseTo[i]));
		j["profiles"].push_back(jp);
	}
	return j;
}

CollisionConfig Plu::LoadCollisionConfig(const JSON& json)
{
	if (!json.is_object() || !json.contains("channels") || !json.contains("profiles"))
		return BuildDefaultCollisionConfig();

	CollisionConfig cfg;
	for (const auto& c : json["channels"])
		cfg.ChannelNames.PushBack(String(c.get<std::string>().c_str()));

	for (const auto& jp : json["profiles"])
	{
		CollisionProfile prof;
		prof.Name = String(jp.value("name", std::string("Profile")).c_str());
		prof.ObjectType = jp.value("objectType", 0);
		if (jp.contains("responses"))
			for (const auto& r : jp["responses"])
				prof.ResponseTo.PushBack(static_cast<CollisionResponse>(r.get<UInt8>()));
		cfg.Profiles.PushBack(prof);
	}

	if (cfg.ChannelNames.IsEmpty() || cfg.Profiles.IsEmpty())
		return BuildDefaultCollisionConfig();

	cfg.NormalizeProfiles();
	return cfg;
}

CollisionConfig& Plu::ActiveCollisionConfig()
{
	static CollisionConfig sActive = BuildDefaultCollisionConfig();
	return sActive;
}
