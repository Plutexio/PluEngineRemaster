//
// Created by Plutex on 9/26/26.
//

#ifndef PLUENGINE_PARTICLEBLOCKSTORAGE_H
#define PLUENGINE_PARTICLEBLOCKSTORAGE_H

#include "PluEngine/Core.h"
#include "PluEngine/Effects/Particles/CompiledParticleSystem.h"
#include "Array/Array.h"

namespace Plu
{
	// SoA store of one emitter's live particles: one float array per used column, [0, Alive()) live.
	//
	// Two modes. Dense (default): killing a particle swap-removes it, so order is not preserved. Ordered
	// (ribbons): compaction is stable, so [0, Alive()) stays in spawn order, oldest first — a ribbon is
	// drawn by walking it. Stable compaction instead of a head/tail ring: the executor needs the live
	// range contiguous, and ribbon emitters are small (thousands), where an O(n) compaction is noise.
	//
	// Optional per-particle history (per-particle ribbons): HistoryFloats() floats per particle, moved
	// together with the columns on compaction.
	class PLUEFFECTS_API ParticleBlockStorage
	{
	public:
		ParticleBlockStorage() = default;
		// mColumns points into mData: a member-wise copy would alias another store's buffers.
		ParticleBlockStorage(const ParticleBlockStorage&) = delete;
		ParticleBlockStorage& operator=(const ParticleBlockStorage&) = delete;

		// Selects which columns exist, the hard particle cap, the compaction mode and the per-particle
		// history size (0 = none). Drops all particles.
		void Configure(ParticleAttributeMask usedColumns, UInt32 maxParticles, bool ordered = false, UInt32 historyFloats = 0);

		[[nodiscard]] UInt32 Alive() const { return mAlive; }
		[[nodiscard]] UInt32 MaxParticles() const { return mMaxParticles; }
		[[nodiscard]] ParticleAttributeMask UsedColumns() const { return mUsed; }
		[[nodiscard]] bool IsOrdered() const { return mOrdered; }
		[[nodiscard]] UInt32 HistoryFloats() const { return mHistoryFloats; }
		// Particle i's history is [i * HistoryFloats(), (i + 1) * HistoryFloats()). Null without history.
		[[nodiscard]] float* History() { return mHistoryFloats ? mHistory.Data() : nullptr; }
		[[nodiscard]] const float* History() const { return mHistoryFloats ? mHistory.Data() : nullptr; }

		// Null when the column was not allocated.
		[[nodiscard]] float* Column(EParticleColumn column) { return mColumns[static_cast<UInt32>(column)]; }
		[[nodiscard]] const float* Column(EParticleColumn column) const { return mColumns[static_cast<UInt32>(column)]; }
		[[nodiscard]] float* Column(UInt32 column) { return mColumns[column]; }
		[[nodiscard]] const float* Column(UInt32 column) const { return mColumns[column]; }

		// Appends up to `count` zero-initialised particles (fewer when the cap is hit). Returns the first
		// new index; the number actually added is Alive() - first. Growing invalidates Column() pointers.
		UInt32 AllocateRange(UInt32 count);

		// Removes every particle with Age >= Lifetime (swap-remove, or stable when ordered). Returns how many
		// were removed.
		UInt32 CompactDead();

		void Clear() { mAlive = 0; }

		[[nodiscard]] UInt64 GetAllocatedBytes() const;

	private:
		void GrowTo(UInt32 capacity);

		UInt32 CompactDense();
		UInt32 CompactOrdered();

		DynamicArray<float> mData[kParticleColumnCount];
		DynamicArray<float> mHistory;
		UInt32 mHistoryFloats = 0;
		bool mOrdered = false;
		float* mColumns[kParticleColumnCount] = {};
		ParticleAttributeMask mUsed = kAlwaysColumnMask;
		UInt32 mAlive = 0;
		UInt32 mCapacity = 0;
		UInt32 mMaxParticles = 0;
	};
}

#endif //PLUENGINE_PARTICLEBLOCKSTORAGE_H
