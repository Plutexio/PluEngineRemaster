//
// Created by Plutex on 9/27/26.
//

#include "PluEngine/Render/RenderParticleLiveness.h"

#include <mutex>

namespace
{
	// Variable-size payload, so a mutex instead of atomics; held for a move in and a copy out.
	std::mutex gLivenessMutex;
	Plu::ParticleLivenessFrame gLiveness;
}

void Plu::PublishParticleLiveness(ParticleLivenessFrame&& frame)
{
	std::lock_guard<std::mutex> lock(gLivenessMutex);
	frame.PublishCount = gLiveness.PublishCount + 1;
	gLiveness = static_cast<ParticleLivenessFrame&&>(frame);
}

void Plu::ReadParticleLiveness(ParticleLivenessFrame& out)
{
	std::lock_guard<std::mutex> lock(gLivenessMutex);
	out = gLiveness;
}
