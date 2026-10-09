//
// Created by Plutex on 9/25/26.
//

#ifndef PLUENGINE_PARTICLESPAWNMODULES_H
#define PLUENGINE_PARTICLESPAWNMODULES_H

#include "PluEngine/Effects/Particles/Nodes/ParticleModuleNode.h"
#include "PluEngine/Effects/Particles/ParticleParam.h"
#include "ParticleSpawnModules.generated.h"

namespace Plu
{
	// Spawn-stage modules: SpawnRate / SpawnBurst decide how many particles are born, the Init*
	// modules set each new particle's starting attributes.

	PLU_STRUCT()
	struct PLUEFFECTS_API SpawnRateModule : ParticleModuleNode
	{
		REFLECTION_BODY_SPAWNRATEMODULE()

		// Particles per second, integrated from the tick delta.
		PLU_PROPERTY()
		float Rate = 50.0f;

		[[nodiscard]] EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
		void BuildPins() override { AddParticleInput(); AddParticleOutput(); BuildDataPinsFromReflection(); }
		String GetDisplayName() override { return "Spawn Rate"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API SpawnBurstModule : ParticleModuleNode
	{
		REFLECTION_BODY_SPAWNBURSTMODULE()

		PLU_PROPERTY()
		int Count = 20;
		// Seconds after the emitter starts (or restarts a loop) at which the burst fires.
		PLU_PROPERTY()
		float Time = 0.0f;

		[[nodiscard]] EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
		void BuildPins() override { AddParticleInput(); AddParticleOutput(); BuildDataPinsFromReflection(); }
		String GetDisplayName() override { return "Spawn Burst"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API InitLifetimeModule : ParticleModuleNode
	{
		REFLECTION_BODY_INITLIFETIMEMODULE()

		PLU_PROPERTY()
		ParticleParamFloat Lifetime = ParticleParamFloat(1.0f);

		[[nodiscard]] EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
		void BuildPins() override { AddParticleInput(); AddParticleOutput(); AddParamPin("Lifetime", "float"); }
		String GetDisplayName() override { return "Init Lifetime"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API InitLocationModule : ParticleModuleNode
	{
		REFLECTION_BODY_INITLOCATIONMODULE()

		PLU_PROPERTY()
		EParticleSpawnShape Shape = EParticleSpawnShape::Point;
		// Sphere radius; Cone reach (distance from the apex).
		PLU_PROPERTY()
		float Radius = 0.1f;
		// Box half extent per axis, in spawner space.
		PLU_PROPERTY()
		Vec3 BoxExtent = Vec3(0.1f);
		// Cone only: half angle in degrees around the spawner's forward (-Z). The cone is a solid spherical
		// sector with its apex at the spawner (+ Offset), filled uniformly; 180 = full sphere.
		PLU_PROPERTY()
		float ConeAngle = 30.0f;
		PLU_PROPERTY()
		Vec3 Offset = Vec3(0.0f);

		[[nodiscard]] EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
		void BuildPins() override { AddParticleInput(); AddParticleOutput(); }
		String GetDisplayName() override { return "Init Location"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API InitVelocityModule : ParticleModuleNode
	{
		REFLECTION_BODY_INITVELOCITYMODULE()

		// Direction is in spawner space (forward is -Z, like the engine's forward vector); Speed in m/s. Cone spreads it by ConeAngle degrees.
		PLU_PROPERTY()
		Vec3 Direction = Vec3(0.0f, 0.0f, -1.0f);
		PLU_PROPERTY()
		ParticleParamFloat Speed = ParticleParamFloat(5.0f);
		PLU_PROPERTY()
		float ConeAngle = 0.0f;
		// Added after the directional part, per axis (m/s).
		PLU_PROPERTY()
		ParticleParamVec3 Randomness;

		[[nodiscard]] EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
		void BuildPins() override { AddParticleInput(); AddParticleOutput(); AddParamPin("Speed", "float"); }
		String GetDisplayName() override { return "Init Velocity"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API InitSizeModule : ParticleModuleNode
	{
		REFLECTION_BODY_INITSIZEMODULE()

		// Sprite width/height in metres (full extent, not half).
		PLU_PROPERTY()
		ParticleParamFloat Size = ParticleParamFloat(0.1f);
		// Height / width; 1 = square.
		PLU_PROPERTY()
		float AspectRatio = 1.0f;

		[[nodiscard]] EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
		void BuildPins() override { AddParticleInput(); AddParticleOutput(); AddParamPin("Size", "float"); }
		String GetDisplayName() override { return "Init Size"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API InitColorModule : ParticleModuleNode
	{
		REFLECTION_BODY_INITCOLORMODULE()

		PLU_PROPERTY()
		ParticleParamColor Color;

		[[nodiscard]] EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
		void BuildPins() override { AddParticleInput(); AddParticleOutput(); AddParamPin("Color", "Vec4"); }
		String GetDisplayName() override { return "Init Color"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API InitRotationModule : ParticleModuleNode
	{
		REFLECTION_BODY_INITROTATIONMODULE()

		// Radians.
		PLU_PROPERTY()
		ParticleParamFloat Rotation = ParticleParamFloat(0.0f);

		[[nodiscard]] EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
		void BuildPins() override { AddParticleInput(); AddParticleOutput(); AddParamPin("Rotation", "float"); }
		String GetDisplayName() override { return "Init Rotation"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	// Gives every particle a random sprite atlas frame in [FirstFrame, LastFrame], so one atlas can hold
	// several variants (debris, sparks, smoke puffs). Fixed for the particle's life; with a SubUV Animation
	// module the animation starts there instead. The atlas size comes from the Sprite Renderer.
	PLU_STRUCT()
	struct PLUEFFECTS_API InitSubUVFrameModule : ParticleModuleNode
	{
		REFLECTION_BODY_INITSUBUVFRAMEMODULE()

		// Zero-based, row by row (left to right, top to bottom).
		PLU_PROPERTY()
		int FirstFrame = 0;
		// -1 = the last frame of the atlas. Equal to FirstFrame = every particle gets that one frame.
		PLU_PROPERTY()
		int LastFrame = -1;

		[[nodiscard]] EParticleModuleStage GetStage() const override { return EParticleModuleStage::Spawn; }
		void BuildPins() override { AddParticleInput(); AddParticleOutput(); BuildDataPinsFromReflection(); }
		String GetDisplayName() override { return "Init SubUV Frame"; }
		void Compile(ParticleCompileContext& ctx) override;
	};
}

#endif //PLUENGINE_PARTICLESPAWNMODULES_H
