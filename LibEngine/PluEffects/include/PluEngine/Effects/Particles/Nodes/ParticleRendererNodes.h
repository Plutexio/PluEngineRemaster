//
// Created by Plutex on 9/25/26.
//

#ifndef PLUENGINE_PARTICLERENDERERNODES_H
#define PLUENGINE_PARTICLERENDERERNODES_H

#include "PluEngine/Effects/Particles/Nodes/ParticleModuleNode.h"
#include "PluEngine/AssetTypes/Texture/Texture.h"
#include "PluEngine/Effects/Particles/ParticleAttributes.h"
#include "Pointers/TUsePointer.h"
#include "ParticleRendererNodes.generated.h"

namespace Plu
{
	// Renderer modules describe how an emitter is drawn; they do no per-particle work. An emitter may have
	// one of each kind, both drawn from the same particles — Sprite + Ribbon (PerParticle) gives every
	// particle a sprite head with a trail behind it. A second module of the same kind replaces the first
	// (the compiler warns).

	PLU_STRUCT()
	struct PLUEFFECTS_API SpriteRendererModule : ParticleModuleNode
	{
		REFLECTION_BODY_SPRITERENDERERMODULE()

		PLU_PROPERTY()
		TUsePointer<TextureInfo> Texture;
		PLU_PROPERTY()
		EParticleBlendMode Blend = EParticleBlendMode::Additive;
		PLU_PROPERTY()
		EParticleFacingMode Facing = EParticleFacingMode::CameraFacing;
		// Atlas layout for SubUVAnimation.
		PLU_PROPERTY()
		int SubUVColumns = 1;
		PLU_PROPERTY()
		int SubUVRows = 1;
		// VelocityStretched: extra length per m/s of speed.
		PLU_PROPERTY()
		float StretchFactor = 0.05f;

		[[nodiscard]] EParticleModuleStage GetStage() const override { return EParticleModuleStage::Renderer; }
		void BuildPins() override { AddParticleInput(); AddParticleOutput(); }
		String GetDisplayName() override { return "Sprite Renderer"; }
		void Compile(ParticleCompileContext& ctx) override;
	};

	PLU_STRUCT()
	struct PLUEFFECTS_API RibbonRendererModule : ParticleModuleNode
	{
		REFLECTION_BODY_RIBBONRENDERERMODULE()

		PLU_PROPERTY()
		TUsePointer<TextureInfo> Texture;
		PLU_PROPERTY()
		EParticleBlendMode Blend = EParticleBlendMode::Additive;
		// PerEmitter: one ribbon through all live particles in spawn order (a trail behind a moving spawner).
		// PerParticle: every particle drags its own trail (tracers, sparks); MaxParticles is clamped.
		PLU_PROPERTY()
		EParticleRibbonMode Mode = EParticleRibbonMode::PerEmitter;
		// Full width in metres. Size Over Life / Size By Speed taper it: width x (size / size at birth).
		PLU_PROPERTY()
		float Width = 0.05f;
		// PerParticle only: trail samples per particle, one every 1/60 s (16 = a quarter-second trail).
		PLU_PROPERTY()
		int HistoryLength = 16;

		[[nodiscard]] EParticleModuleStage GetStage() const override { return EParticleModuleStage::Renderer; }
		void BuildPins() override { AddParticleInput(); AddParticleOutput(); }
		String GetDisplayName() override { return "Ribbon Renderer"; }
		void Compile(ParticleCompileContext& ctx) override;
	};
}

#endif //PLUENGINE_PARTICLERENDERERNODES_H
