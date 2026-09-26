//
// Created by Plutex on 9/26/26.
//

#ifndef PLUENGINE_PARTICLEEMITTERINSTANCE_H
#define PLUENGINE_PARTICLEEMITTERINSTANCE_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Effects/Particles/CompiledParticleSystem.h"
#include "PluEngine/Effects/Particles/ParticleBlockStorage.h"
#include "PluEngine/Effects/Particles/ParticleBlockExecutor.h"

#include <algorithm>

namespace Plu
{
	// Per-particle ribbons: a new trail sample is taken this often (seconds), so a trail covers
	// RibbonHistoryLength x this, whatever the frame rate. Sample 0 always follows the particle.
	constexpr float kRibbonHistorySampleInterval = 1.0f / 60.0f;
	// Floats per history sample (xyz + pad: a vec4 in std430).
	constexpr UInt32 kRibbonHistorySampleFloats = 4;

	// Storage layout a program needs: ordered for ribbons, plus a position history per particle for
	// per-particle ribbons.
	inline bool ParticleProgramNeedsOrderedStorage(const CompiledEmitter& program)
	{
		return program.HasRibbon();
	}
	inline UInt32 ParticleProgramHistorySamples(const CompiledEmitter& program)
	{
		return program.HasRibbon() && program.Ribbon.RibbonMode == EParticleRibbonMode::PerParticle
			? std::max<UInt32>(2, program.Ribbon.RibbonHistoryLength) : 0;
	}

	struct ParticleTickParams
	{
		float DeltaTime = 0.0f;
		Vec3 Location = Vec3(0.0f);
		Quaternion Rotation = Quaternion(1.0f, 0.0f, 0.0f, 0.0f);

		// One block of parameter values (CompiledParticleSystem::ParameterLayout order). Null = use Defaults.
		const float* ParameterValues = nullptr;
		UInt32 ParameterValueCount = 0;
		const float* ParameterDefaults = nullptr;
		UInt32 ParameterDefaultCount = 0;

		bool Emit = true;          // false = let live particles finish, spawn nothing
		UInt32 ExtraSpawn = 0;     // additional particles this tick (external bursts, stress tests)
	};

	// One running emitter: its program, particle store, clock and spawn accumulators. Ticks on whichever
	// thread owns it (the render thread for scene effects). Holds a COPY of the program: a snapshot
	// pointer must never be cached across frames.
	class PLUEFFECTS_API ParticleEmitterInstance
	{
	public:
		ParticleEmitterInstance() = default;
		ParticleEmitterInstance(const ParticleEmitterInstance&) = delete; // owns a ParticleBlockStorage
		ParticleEmitterInstance& operator=(const ParticleEmitterInstance&) = delete;

		// Adopts a program. Restarts the emitter when the column layout or particle cap changed; otherwise
		// keeps live particles so a live edit of an op constant takes effect on the next tick.
		void SetProgram(const CompiledEmitter& program);
		[[nodiscard]] const CompiledEmitter& GetProgram() const { return mProgram; }

		// Clock, accumulators and particles back to the start.
		void Reset();
		// Drops live particles only (the clock keeps running).
		void ClearParticles() { mStorage.Clear(); }

		void Tick(const ParticleTickParams& params);

		[[nodiscard]] UInt32 Alive() const { return mStorage.Alive(); }
		[[nodiscard]] float GetTime() const { return mTime; }
		// Emission over (non-looping past its duration) and nothing alive.
		// A disabled emitter never emits, so it counts as done.
		[[nodiscard]] bool IsFinished() const { return (mEmissionDone || !mProgram.Enabled) && mStorage.Alive() == 0; }
		[[nodiscard]] bool IsEmissionDone() const { return mEmissionDone; }

		[[nodiscard]] ParticleBlockStorage& GetStorage() { return mStorage; }
		[[nodiscard]] const ParticleBlockStorage& GetStorage() const { return mStorage; }

		// Axis-aligned bounds of the live particles; false when none.
		bool ComputeBounds(Vec3& outMin, Vec3& outMax) const;

		// Per-op timing. Off by default (a clock read per op per block). When on, ticks also record each op
		// to the Profiler under "Particles/<emitter>/<spawn|update> <index> <OpName>".
		void SetProfileOps(bool enabled);
		[[nodiscard]] const ParticleOpTimings& GetOpTimings() const { return mTimings; }
		// Times of the last tick only (empty unless that tick ran with profiling on).
		[[nodiscard]] const ParticleOpTimings& GetLastTickOpTimings() const { return mTickTimings; }
		[[nodiscard]] bool IsProfilingOps() const { return mProfileOps; }
		void ResetOpTimings();

	private:
		void RecordOpTimings();
		// Per-particle ribbons: shifts in a new sample when due and pins sample 0 to the current position.
		void UpdateRibbonHistory(float deltaTime);
		// Fills the whole history of particles [first, end) with their spawn position (zero-length trail).
		void InitRibbonHistory(UInt32 first, UInt32 end);

		CompiledEmitter mProgram;
		ParticleBlockStorage mStorage;
		float mTime = 0.0f;
		float mSpawnAccumulator = 0.0f;
		bool mBurstFired = false;
		bool mEmissionDone = false;
		UInt32 mRngState = 0x9E3779B9u;
		float mRibbonSampleAccumulator = 0.0f;
		bool mProfileOps = false;
		ParticleOpTimings mTimings;
		ParticleOpTimings mTickTimings;
	};
}

#endif //PLUENGINE_PARTICLEEMITTERINSTANCE_H
