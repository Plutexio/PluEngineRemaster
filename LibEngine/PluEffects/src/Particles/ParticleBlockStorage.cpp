//
// Created by Plutex on 9/26/26.
//

#include "PluEngine/Effects/Particles/ParticleBlockStorage.h"
#include "PluEngine/Timer.h"

#include <cstring>

namespace Plu
{
	void ParticleBlockStorage::Configure(ParticleAttributeMask usedColumns, UInt32 maxParticles, bool ordered, UInt32 historyFloats)
	{
		mUsed = usedColumns | kAlwaysColumnMask;
		mMaxParticles = maxParticles;
		mOrdered = ordered;
		mHistoryFloats = historyFloats;
		mHistory = DynamicArray<float>();
		mAlive = 0;
		mCapacity = 0;
		for (UInt32 c = 0; c < kParticleColumnCount; ++c) {
			mData[c] = DynamicArray<float>();
			mColumns[c] = nullptr;
		}
	}

	void ParticleBlockStorage::GrowTo(UInt32 capacity)
	{
		for (UInt32 c = 0; c < kParticleColumnCount; ++c) {
			if (!(mUsed & (1u << c))) continue;
			mData[c].Resize(capacity);
			mColumns[c] = mData[c].Data();
		}
		if (mHistoryFloats) mHistory.Resize(capacity * mHistoryFloats);
		mCapacity = capacity;
	}

	UInt32 ParticleBlockStorage::AllocateRange(UInt32 count)
	{
		const UInt32 first = mAlive;
		const UInt32 room = mMaxParticles > mAlive ? mMaxParticles - mAlive : 0;
		if (count > room) count = room;
		if (count == 0) return first;

		const UInt32 needed = mAlive + count;
		if (needed > mCapacity) {
			// Doubling keeps the number of growths (each a realloc of every column) logarithmic.
			UInt32 capacity = mCapacity == 0 ? 1024u : mCapacity;
			while (capacity < needed) capacity *= 2;
			if (capacity > mMaxParticles) capacity = mMaxParticles;
			GrowTo(capacity);
		}

		// Slots past the live range hold stale data from earlier swap-removes: new particles start zeroed.
		for (UInt32 c = 0; c < kParticleColumnCount; ++c) {
			if (mColumns[c]) std::memset(mColumns[c] + first, 0, sizeof(float) * count);
		}
		if (mHistoryFloats)
			std::memset(mHistory.Data() + static_cast<UInt64>(first) * mHistoryFloats, 0, sizeof(float) * count * mHistoryFloats);
		mAlive = needed;
		return first;
	}

	UInt32 ParticleBlockStorage::CompactDead()
	{
		PLU_PROFILE_SCOPE("Particles/Compact");
		return mOrdered ? CompactOrdered() : CompactDense();
	}

	UInt32 ParticleBlockStorage::CompactDense()
	{
		const float* age = mColumns[static_cast<UInt32>(EParticleColumn::Age)];
		const float* life = mColumns[static_cast<UInt32>(EParticleColumn::Lifetime)];
		if (!age || !life) return 0;

		// Live columns, gathered once so the copy below is a plain loop.
		float* live[kParticleColumnCount];
		UInt32 liveCount = 0;
		for (UInt32 c = 0; c < kParticleColumnCount; ++c) if (mColumns[c]) live[liveCount++] = mColumns[c];

		UInt32 removed = 0;
		UInt32 i = 0;
		while (i < mAlive) {
			if (age[i] >= life[i]) {
				--mAlive;
				++removed;
				// Re-check slot i afterwards: the particle moved in may be dead as well.
				if (i < mAlive) {
					for (UInt32 c = 0; c < liveCount; ++c) live[c][i] = live[c][mAlive];
					if (mHistoryFloats)
						std::memcpy(mHistory.Data() + static_cast<UInt64>(i) * mHistoryFloats,
						            mHistory.Data() + static_cast<UInt64>(mAlive) * mHistoryFloats, sizeof(float) * mHistoryFloats);
				}
			} else {
				++i;
			}
		}
		return removed;
	}

	UInt32 ParticleBlockStorage::CompactOrdered()
	{
		const float* age = mColumns[static_cast<UInt32>(EParticleColumn::Age)];
		const float* life = mColumns[static_cast<UInt32>(EParticleColumn::Lifetime)];
		if (!age || !life) return 0;

		// Skip the untouched prefix; with a constant lifetime the dead are all at the front, so this is
		// usually a single shift of every column.
		UInt32 write = 0;
		while (write < mAlive && age[write] < life[write]) ++write;
		if (write == mAlive) return 0;

		for (UInt32 read = write + 1; read < mAlive; ++read) {
			if (age[read] >= life[read]) continue;
			for (UInt32 c = 0; c < kParticleColumnCount; ++c)
				if (mColumns[c]) mColumns[c][write] = mColumns[c][read];
			if (mHistoryFloats)
				std::memcpy(mHistory.Data() + static_cast<UInt64>(write) * mHistoryFloats,
				            mHistory.Data() + static_cast<UInt64>(read) * mHistoryFloats, sizeof(float) * mHistoryFloats);
			++write;
		}
		const UInt32 removed = mAlive - write;
		mAlive = write;
		return removed;
	}

	UInt64 ParticleBlockStorage::GetAllocatedBytes() const
	{
		UInt64 bytes = static_cast<UInt64>(mHistory.Size()) * sizeof(float);
		for (UInt32 c = 0; c < kParticleColumnCount; ++c) bytes += static_cast<UInt64>(mData[c].Size()) * sizeof(float);
		return bytes;
	}
}
