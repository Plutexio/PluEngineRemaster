//
// Created by Plutex on 9/25/26.
//

#ifndef PLUENGINE_PARTICLESOURCENODES_H
#define PLUENGINE_PARTICLESOURCENODES_H

#include "PluEngine/Effects/Particles/Nodes/ParticleModuleNode.h"
#include "ParticleSourceNodes.generated.h"

namespace Plu
{
	// Exposes one per-particle attribute as a data output, to wire into a module's parameter pin.
	// The value is per particle, so it cannot be folded to a constant: EvaluateDataOutput stays
	// unimplemented and the compiler turns a direct wire into an Attribute operand.
	PLU_STRUCT()
	struct PLUEFFECTS_API ParticleAttributeNode : ParticleModuleNode
	{
		REFLECTION_BODY_PARTICLEATTRIBUTENODE()

		PLU_PROPERTY()
		EParticleAttribute Attribute = EParticleAttribute::NormalizedAge;

		void BuildPins() override
		{
			AddPin("Value", EPinDirection::Output, EPinCategory::Data, ParticleAttributePinTypeId(Attribute));
		}
		String GetDisplayName() override { return "Particle Attribute"; }
	};
}

#endif //PLUENGINE_PARTICLESOURCENODES_H
