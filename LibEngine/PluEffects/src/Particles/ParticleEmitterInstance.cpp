//
// Created by Plutex on 9/26/26.
//

#include "PluEngine/Effects/Particles/ParticleEmitterInstance.h"
#include "PluEngine/Profiler.h"
#include "PluEngine/Timer.h"

#include <cmath>
#include <cstring>

namespace Plu
{
	void ParticleEmitterInstance::SetProgram(const CompiledEmitter& program)
	{
		const bool ordered = ParticleProgramNeedsOrderedStorage(program);
		const UInt32 historyFloats = ParticleProgramHistorySamples(program) * kRibbonHistorySampleFloats;
		const bool layoutChanged = program.UsedAttributes != mProgram.UsedAttributes
		                        || program.MaxParticles != mProgram.MaxParticles
		                        || ordered != mStorage.IsOrdered()
		                        || historyFloats != mStorage.HistoryFloats()
		                        || mStorage.MaxParticles() == 0;
		mProgram = program;
		if (layoutChanged) {
			mStorage.Configure(mProgram.UsedAttributes, mProgram.MaxParticles, ordered, historyFloats);
			Reset();
		}
	}

	void ParticleEmitterInstance::Reset()
	{
		mStorage.Clear();
		mTime = 0.0f;
		mSpawnAccumulator = 0.0f;
		mBurstFired = false;
		mEmissionDone = false;
		mRngState = 0x9E3779B9u;
		mRibbonSampleAccumulator = 0.0f;
	}

	void ParticleEmitterInstance::SetProfileOps(bool enabled)
	{
		// Last-tick times must not outlive the profiling that produced them (the debug panel shows them as current).
		if (!enabled && mProfileOps) mTickTimings = ParticleOpTimings();
		mProfileOps = enabled;
	}

	void ParticleEmitterInstance::ResetOpTimings()
	{
		mTimings = ParticleOpTimings();
	}

	void ParticleEmitterInstance::Tick(const ParticleTickParams& params)
	{
		PLU_PROFILE_SCOPE("Particles/EmitterTick");

		ParticleExecContext ctx;
		ctx.Program = &mProgram;
		ctx.Storage = &mStorage;
		ctx.ParameterValues = params.ParameterValues;
		ctx.ParameterValueCount = params.ParameterValueCount;
		ctx.ParameterDefaults = params.ParameterDefaults;
		ctx.ParameterDefaultCount = params.ParameterDefaultCount;
		ctx.DeltaTime = params.DeltaTime;
		ctx.Location = params.Location;
		ctx.Rotation = params.Rotation;
		ctx.RngState = mRngState;
		if (mProfileOps) {
			mTickTimings = ParticleOpTimings();
			ctx.Timings = &mTickTimings;
		}

		// 1. Advance and update what is already alive, then drop the dead in one compaction.
		ParticleBlockExecutor::RunUpdate(ctx);
		mStorage.CompactDead();
		if (mStorage.HistoryFloats()) UpdateRibbonHistory(params.DeltaTime);

		// 2. Emission clock. Duration 0 = endless.
		if (mProgram.Enabled && !mEmissionDone) {
			mTime += params.DeltaTime;
			if (mProgram.Duration > 0.0f && mTime >= mProgram.Duration) {
				if (mProgram.Loop) {
					mTime = std::fmod(mTime, mProgram.Duration);
					mBurstFired = false;
				} else {
					mEmissionDone = true;
				}
			}
		}

		// 3. Spawn: rate integrated from dt, the burst once its time is reached, plus caller extras.
		UInt32 toSpawn = params.ExtraSpawn;
		if (mProgram.Enabled && params.Emit && !mEmissionDone) {
			mSpawnAccumulator += mProgram.SpawnRate * params.DeltaTime;
			const float whole = std::floor(mSpawnAccumulator);
			mSpawnAccumulator -= whole;
			toSpawn += static_cast<UInt32>(whole);
			if (!mBurstFired && mProgram.BurstCount > 0 && mTime >= mProgram.BurstTime) {
				mBurstFired = true;
				toSpawn += mProgram.BurstCount;
			}
		}

		if (toSpawn > 0) {
			const UInt32 first = mStorage.AllocateRange(toSpawn);
			ParticleBlockExecutor::RunSpawn(ctx, first, mStorage.Alive());
			if (mStorage.HistoryFloats()) InitRibbonHistory(first, mStorage.Alive());
		}

		mRngState = ctx.RngState;
		if (mProfileOps) RecordOpTimings();
	}

	void ParticleEmitterInstance::UpdateRibbonHistory(float deltaTime)
	{
		PLU_PROFILE_SCOPE("Particles/RibbonHistory");
		const UInt32 alive = mStorage.Alive();
		const UInt32 stride = mStorage.HistoryFloats();
		float* history = mStorage.History();
		if (alive == 0 || !history) return;

		// One shift for everybody when a sample is due. A long hitch takes one sample, not a burst of
		// identical ones.
		mRibbonSampleAccumulator += deltaTime;
		const bool shift = mRibbonSampleAccumulator >= kRibbonHistorySampleInterval;
		if (shift) {
			mRibbonSampleAccumulator -= kRibbonHistorySampleInterval;
			if (mRibbonSampleAccumulator >= kRibbonHistorySampleInterval) mRibbonSampleAccumulator = 0.0f;
		}

		const float* x = mStorage.Column(EParticleColumn::PosX);
		const float* y = mStorage.Column(EParticleColumn::PosY);
		const float* z = mStorage.Column(EParticleColumn::PosZ);
		for (UInt32 i = 0; i < alive; ++i) {
			float* row = history + static_cast<UInt64>(i) * stride;
			if (shift) std::memmove(row + kRibbonHistorySampleFloats, row, sizeof(float) * (stride - kRibbonHistorySampleFloats));
			row[0] = x[i]; row[1] = y[i]; row[2] = z[i]; row[3] = 0.0f;
		}
	}

	void ParticleEmitterInstance::InitRibbonHistory(UInt32 first, UInt32 end)
	{
		const UInt32 stride = mStorage.HistoryFloats();
		float* history = mStorage.History();
		if (!history) return;
		const float* x = mStorage.Column(EParticleColumn::PosX);
		const float* y = mStorage.Column(EParticleColumn::PosY);
		const float* z = mStorage.Column(EParticleColumn::PosZ);
		for (UInt32 i = first; i < end; ++i) {
			float* row = history + static_cast<UInt64>(i) * stride;
			for (UInt32 s = 0; s < stride; s += kRibbonHistorySampleFloats) {
				row[s] = x[i]; row[s + 1] = y[i]; row[s + 2] = z[i]; row[s + 3] = 0.0f;
			}
		}
	}

	void ParticleEmitterInstance::RecordOpTimings()
	{
		auto accumulate = [](DynamicArray<double>& total, const DynamicArray<double>& tick) {
			if (total.Size() < tick.Size()) total.Resize(tick.Size());
			for (UInt32 i = 0; i < tick.Size(); ++i) total[i] += tick[i];
		};
		accumulate(mTimings.SpawnMs, mTickTimings.SpawnMs);
		accumulate(mTimings.UpdateMs, mTickTimings.UpdateMs);

		Profiler* profiler = Profiler::GetInstance();
		auto record = [&](const char* stage, const DynamicArray<ParticleOp>& ops, const DynamicArray<double>& ms) {
			for (UInt32 i = 0; i < ops.Size() && i < ms.Size(); ++i) {
				String name = String("Particles/") + mProgram.Name + "/" + stage + " " + String::FromInt(i) + " " + ParticleOpName(ops[i].Code);
				profiler->Record(name, static_cast<float>(ms[i]));
			}
		};
		record("spawn", mProgram.SpawnOps, mTickTimings.SpawnMs);
		record("update", mProgram.UpdateOps, mTickTimings.UpdateMs);
	}

	bool ParticleEmitterInstance::ComputeBounds(Vec3& outMin, Vec3& outMax) const
	{
		const UInt32 alive = mStorage.Alive();
		if (alive == 0) return false;
		const float* x = mStorage.Column(EParticleColumn::PosX);
		const float* y = mStorage.Column(EParticleColumn::PosY);
		const float* z = mStorage.Column(EParticleColumn::PosZ);
		outMin = Vec3(x[0], y[0], z[0]);
		outMax = outMin;
		for (UInt32 i = 1; i < alive; ++i) {
			outMin = Vec3(std::min(outMin.x, x[i]), std::min(outMin.y, y[i]), std::min(outMin.z, z[i]));
			outMax = Vec3(std::max(outMax.x, x[i]), std::max(outMax.y, y[i]), std::max(outMax.z, z[i]));
		}
		return true;
	}
}
