//
// Created by Plutex on 1/3/26.
//
#include "PluEngine/PluUUID.h"

#include <random>

static std::random_device s_RandomDevice;
static std::mt19937_64 s_RandomEngine(s_RandomDevice());
static std::uniform_int_distribution<UInt64> s_UniformDistribution;

Plu::PluUUID::PluUUID()
{
	mUUID = s_UniformDistribution(s_RandomEngine);
	while (mUUID == 0) {
		mUUID = s_UniformDistribution(s_RandomEngine);
	}
}

Plu::PluUUID::PluUUID(UInt64 UUID)
{
	mUUID = UUID;
}

Plu::PluUUID::PluUUID(const PluUUID& other)
{
	mUUID = other.mUUID;
}

Plu::PluUUID& Plu::PluUUID::operator=(const PluUUID& other)
{
	mUUID = other.mUUID;
	return *this;
}