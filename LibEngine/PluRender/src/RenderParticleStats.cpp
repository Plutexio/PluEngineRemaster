//
// Created by Plutex on 2026-09-15.
//

#include "PluEngine/Render/RenderParticleStats.h"

#include <atomic>
#include <mutex>

namespace
{
	std::atomic<bool> gParticleDebugStatsRequested{false};
	std::atomic<bool> gParticleOpTimingsRequested{false};

	// Variable-size payload, so a mutex instead of the atomics the fixed-size stats use. Held only
	// for a move in and a copy out.
	std::mutex gParticleDebugStatsMutex;
	Plu::ParticleDebugStats gParticleDebugStats;
}

void Plu::RequestParticleDebugStats()
{
	gParticleDebugStatsRequested.store(true, std::memory_order_relaxed);
}

bool Plu::ConsumeParticleDebugStatsRequest()
{
	return gParticleDebugStatsRequested.exchange(false, std::memory_order_relaxed);
}

void Plu::PublishParticleDebugStats(ParticleDebugStats&& stats)
{
	std::lock_guard<std::mutex> lock(gParticleDebugStatsMutex);
	stats.PublishCount = gParticleDebugStats.PublishCount + 1;
	gParticleDebugStats = static_cast<ParticleDebugStats&&>(stats);
}

Plu::ParticleDebugStats Plu::GetParticleDebugStats()
{
	std::lock_guard<std::mutex> lock(gParticleDebugStatsMutex);
	return gParticleDebugStats;
}

void Plu::RequestParticleOpTimings()
{
	gParticleOpTimingsRequested.store(true, std::memory_order_relaxed);
}

bool Plu::ConsumeParticleOpTimingsRequest()
{
	return gParticleOpTimingsRequested.exchange(false, std::memory_order_relaxed);
}
