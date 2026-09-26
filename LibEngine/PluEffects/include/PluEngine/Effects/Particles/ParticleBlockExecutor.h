//
// Created by Plutex on 9/26/26.
//

#ifndef PLUENGINE_PARTICLEBLOCKEXECUTOR_H
#define PLUENGINE_PARTICLEBLOCKEXECUTOR_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Effects/Particles/CompiledParticleSystem.h"
#include "PluEngine/Effects/Particles/ParticleBlockStorage.h"
#include "Array/Array.h"

namespace Plu
{
	// Wall time spent in each op of a program, accumulated over the ticks since the last reset.
	struct ParticleOpTimings
	{
		DynamicArray<double> SpawnMs;
		DynamicArray<double> UpdateMs;
	};

	struct ParticleExecContext
	{
		const CompiledEmitter* Program = nullptr;
		ParticleBlockStorage* Storage = nullptr;

		// Parameter values for Parameter operands. An index outside Values falls back to Defaults, then 0:
		// the block may be one frame out of step with the program (recompile between the two writes).
		const float* ParameterValues = nullptr;
		UInt32 ParameterValueCount = 0;
		const float* ParameterDefaults = nullptr;
		UInt32 ParameterDefaultCount = 0;

		float DeltaTime = 0.0f;
		Vec3 Location = Vec3(0.0f);
		Quaternion Rotation = Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
		UInt32 RngState = 0x9E3779B9u;

		ParticleOpTimings* Timings = nullptr; // null = no per-op timing
	};

	// Runs a compiled emitter over its SoA store. The loop nest is blocks -> ops -> particles, and every
	// operand is resolved BEFORE the innermost loop (constant/parameter -> local float, attribute ->
	// column pointer, curve -> LUT + scale/bias), so the op switch runs opCount x blockCount times, not
	// opCount x particleCount. The old pull-based graph evaluator is never called from here.
	class PLUEFFECTS_API ParticleBlockExecutor
	{
	public:
		// Spawn ops over [first, last): initialises freshly allocated particles.
		static void RunSpawn(ParticleExecContext& ctx, UInt32 first, UInt32 last);
		// Update ops over [0, Alive()). Death only zeroes Lifetime; the caller compacts once per tick.
		static void RunUpdate(ParticleExecContext& ctx);
	};
}

#endif //PLUENGINE_PARTICLEBLOCKEXECUTOR_H
