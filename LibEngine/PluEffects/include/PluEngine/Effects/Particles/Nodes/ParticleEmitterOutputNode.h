//
// Created by Plutex on 9/25/26.
//

#ifndef PLUENGINE_PARTICLEEMITTEROUTPUTNODE_H
#define PLUENGINE_PARTICLEEMITTEROUTPUTNODE_H

#include "PluEngine/Effects/Particles/Nodes/ParticleModuleNode.h"
#include "ParticleEmitterOutputNode.generated.h"

namespace Plu
{
	// Terminates the module chain: the compiler starts here and walks the "In" link backwards.
	PLU_STRUCT()
	struct PLUEFFECTS_API ParticleEmitterOutputNode : ParticleModuleNode
	{
		REFLECTION_BODY_PARTICLEEMITTEROUTPUTNODE()

		[[nodiscard]] EParticleModuleStage GetStage() const override { return EParticleModuleStage::Output; }
		void BuildPins() override { AddParticleInput(); }
		String GetDisplayName() override { return "Emitter Output"; }
	};
}

#endif //PLUENGINE_PARTICLEEMITTEROUTPUTNODE_H
