//
// Created by Plutex on 9/27/26.
//

#ifndef PLUENGINE_RENDERPARTICLELIVENESS_H
#define PLUENGINE_RENDERPARTICLELIVENESS_H

#include "PluEngine/Core.h"
#include "PluEngine/Core/Objects/EngineObjectHandle.h"
#include "Array/Array.h"

namespace Plu
{
	// Render -> main feedback about particle spawners, the counterpart of the snapshot's spawner state.
	// Always on and O(spawners): four numbers per spawner, taken from counters the tick maintains anyway.
	// (RenderParticleStats is the debug channel — it gathers per particle, on request, throttled.)
	struct ParticleSpawnerLiveness
	{
		UInt64 SpawnerUuid = 0;
		UInt32 AliveParticles = 0;
		// Emitters that still emit (not finished, not stopped).
		UInt32 EmittingEmitters = 0;
		// The newest ActivationVersion whose run has no work left (nothing emitting, nothing alive).
		// Monotonic, so a lost or replayed publish cannot make main see a run "finish" twice or un-finish.
		UInt32 CompletedActivationVersion = 0;
	};

	struct ParticleLivenessFrame
	{
		UInt64 PublishCount = 0;
		EngineObjectHandle SceneHandle;
		DynamicArray<ParticleSpawnerLiveness> Spawners;
	};

	// Render thread, once per particle tick.
	PLURENDER_API void PublishParticleLiveness(ParticleLivenessFrame&& frame);
	// Any thread: one copy of the latest frame under a mutex.
	PLURENDER_API void ReadParticleLiveness(ParticleLivenessFrame& out);
}

#endif //PLUENGINE_RENDERPARTICLELIVENESS_H
