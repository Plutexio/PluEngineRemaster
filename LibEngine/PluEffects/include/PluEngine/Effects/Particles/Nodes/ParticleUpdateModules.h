//
// Created by Plutex on 9/25/26.
//

#ifndef PLUENGINE_PARTICLEUPDATEMODULES_H
#define PLUENGINE_PARTICLEUPDATEMODULES_H

#include "PluEngine/Effects/Particles/Nodes/ParticleModuleNode.h"
#include "PluEngine/Effects/Particles/ParticleParam.h"
#include "ParticleUpdateModules.generated.h"

namespace Plu
{
	// Update-stage modules: run every tick over every live particle, in chain order.

	PLU_STRUCT()
	struct PLUEFFECTS_API GravityModule : ParticleModuleNode
	{
		REFLECTION_BODY_GRAVITYMODULE()

		// m/s^2.
		PLU_PROPERTY()
		Vec3 Gravity = Vec3(0.0f, -9.81f, 0.0f);

		void BuildPins() override { AddParticleInput(); AddParticleOutput(); BuildDataPinsFromReflection(); }
		String GetDisplayName() override { return "Gravity"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API DragModule : ParticleModuleNode
	{
		REFLECTION_BODY_DRAGMODULE()

		// Linear drag coefficient (1/s): velocity decays as exp(-Drag * t).
		PLU_PROPERTY()
		float Drag = 1.0f;

		void BuildPins() override { AddParticleInput(); AddParticleOutput(); BuildDataPinsFromReflection(); }
		String GetDisplayName() override { return "Drag"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API AccelerationModule : ParticleModuleNode
	{
		REFLECTION_BODY_ACCELERATIONMODULE()

		PLU_PROPERTY()
		Vec3 Acceleration = Vec3(0.0f);

		void BuildPins() override { AddParticleInput(); AddParticleOutput(); BuildDataPinsFromReflection(); }
		String GetDisplayName() override { return "Acceleration"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API ColorOverLifeModule : ParticleModuleNode
	{
		REFLECTION_BODY_COLOROVERLIFEMODULE()

		// Multiplied into the particle's spawn colour.
		PLU_PROPERTY()
		ColorGradient Gradient = ColorGradient::Constant(Vec4(1.0f));

		void BuildPins() override { AddParticleInput(); AddParticleOutput(); }
		String GetDisplayName() override { return "Color Over Life"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API SizeOverLifeModule : ParticleModuleNode
	{
		REFLECTION_BODY_SIZEOVERLIFEMODULE()

		// Scale on the spawn size over normalised age.
		PLU_PROPERTY()
		Curve Scale = Curve::Ramp(1.0f, 1.0f);

		void BuildPins() override { AddParticleInput(); AddParticleOutput(); }
		String GetDisplayName() override { return "Size Over Life"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API SizeBySpeedModule : ParticleModuleNode
	{
		REFLECTION_BODY_SIZEBYSPEEDMODULE()

		// Scale on the spawn size, sampled at speed in [0, MaxSpeed] m/s.
		PLU_PROPERTY()
		Curve Scale = Curve::Ramp(1.0f, 1.0f);
		PLU_PROPERTY()
		float MaxSpeed = 10.0f;

		void BuildPins() override { AddParticleInput(); AddParticleOutput(); BuildDataPinsFromReflection(); }
		String GetDisplayName() override { return "Size By Speed"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API RotationRateModule : ParticleModuleNode
	{
		REFLECTION_BODY_ROTATIONRATEMODULE()

		// Radians per second.
		PLU_PROPERTY()
		float Rate = 1.0f;
		// Each particle spins at Rate + a random value in [-RateRandomness, RateRandomness], drawn once at
		// spawn and kept for its whole life. 0 = every particle spins at exactly Rate.
		PLU_PROPERTY()
		float RateRandomness = 0.0f;

		void BuildPins() override { AddParticleInput(); AddParticleOutput(); BuildDataPinsFromReflection(); }
		String GetDisplayName() override { return "Rotation Rate"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API SubUVAnimationModule : ParticleModuleNode
	{
		REFLECTION_BODY_SUBUVANIMATIONMODULE()

		// Plays the sprite atlas once over the particle's life (or loops FramesPerSecond when > 0). With an
		// Init SubUV Frame module in the chain each particle starts at its random frame and wraps around.
		PLU_PROPERTY()
		float FramesPerSecond = 0.0f;

		void BuildPins() override { AddParticleInput(); AddParticleOutput(); BuildDataPinsFromReflection(); }
		String GetDisplayName() override { return "SubUV Animation"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API KillWhenSlowModule : ParticleModuleNode
	{
		REFLECTION_BODY_KILLWHENSLOWMODULE()

		// m/s. Armed only after the particle has exceeded it once.
		PLU_PROPERTY()
		float Speed = 0.1f;

		void BuildPins() override { AddParticleInput(); AddParticleOutput(); BuildDataPinsFromReflection(); }
		String GetDisplayName() override { return "Kill When Slow"; }
		void Compile(ParticleCompileContext& ctx) override;
	};
}

#endif //PLUENGINE_PARTICLEUPDATEMODULES_H
