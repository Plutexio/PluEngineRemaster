//
// Created by Plutex on 9/25/26.
//

#ifndef PLUENGINE_PARTICLEATTRIBUTES_H
#define PLUENGINE_PARTICLEATTRIBUTES_H

#include "PluEngine/Core.h"
#include "ParticleAttributes.generated.h"

namespace Plu
{
	// Where in the emitter's module chain a module runs.
	enum class EParticleModuleStage : UInt8
	{
		Spawn,    // once per newly born particle
		Update,   // every tick over every live particle
		Renderer, // draw description, no per-particle work
		Output    // chain terminator
	};

	// Per-particle values a module can read. Authoring-facing: the compiler maps each one to a
	// column of the SoA store.
	PLU_ENUM(PyNamespace=Plu)
	enum class EParticleAttribute : UInt8
	{
		Age,
		NormalizedAge,
		Speed,
		Position,
		Velocity,
		Seed
	};

	// Where a module places new particles, relative to the spawner.
	PLU_ENUM(PyNamespace=Plu)
	enum class EParticleSpawnShape : UInt8
	{
		Point,
		Sphere,
		Box,
		Cone
	};

	PLU_ENUM(PyNamespace=Plu)
	enum class EParticleBlendMode : UInt8
	{
		Additive,
		AlphaBlend
	};

	PLU_ENUM(PyNamespace=Plu)
	enum class EParticleFacingMode : UInt8
	{
		CameraFacing,
		VelocityStretched
	};

	PLU_ENUM(PyNamespace=Plu)
	enum class EParticleRibbonMode : UInt8
	{
		PerParticle,
		PerEmitter
	};

	// What a spawner component asks its emitters to do. State, never an event: it rides every render
	// snapshot, and restarts/clears are separate monotonic counters (see ParticleSpawnerRenderObject).
	PLU_ENUM(PyExport, PyNamespace=Plu)
	enum class EParticleEmissionState : UInt8
	{
		Stopped,
		Playing,
		Paused
	};

	// Data-pin type id a value of `attribute` exposes.
	inline const char* ParticleAttributePinTypeId(EParticleAttribute attribute)
	{
		return (attribute == EParticleAttribute::Position || attribute == EParticleAttribute::Velocity) ? "Vec3" : "float";
	}
}

#endif //PLUENGINE_PARTICLEATTRIBUTES_H
