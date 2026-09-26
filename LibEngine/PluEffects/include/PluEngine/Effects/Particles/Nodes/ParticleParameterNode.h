//
// Created by Plutex on 9/25/26.
//

#ifndef PLUENGINE_PARTICLEPARAMETERNODE_H
#define PLUENGINE_PARTICLEPARAMETERNODE_H

#include "PluEngine/Effects/Particles/Nodes/ParticleModuleNode.h"
#include "PluEngine/Effects/Particles/ParticleParameter.h"
#include "ParticleParameterNode.generated.h"

namespace Plu
{
	// Reads a ParticleSystem parameter. Bound to the live parameter by ParticleSystem::
	// ResolveParameterReferences (the serialized key is the parameter's name).
	PLU_STRUCT()
	struct PLUEFFECTS_API ParticleParameterNode : ParticleModuleNode
	{
		REFLECTION_BODY_PARTICLEPARAMETERNODE()

		PLU_PROPERTY()
		String ParameterName;

		TUsePointer<IParticleParameter> Parameter;

		void BuildPins() override
		{
			AddPin("Value", EPinDirection::Output, EPinCategory::Data,
			       Parameter ? Parameter->PinTypeId : String());
		}

		String GetDisplayName() override
		{
			if (Parameter) return Parameter->Name;
			return ParameterName.IsEmpty() ? String("Parameter") : ParameterName;
		}

		// Authored default. Not a per-frame read: parameter values change per instance, so the compiler
		// binds a direct wire as a Parameter operand instead of folding this into a constant.
		bool EvaluateDataOutput(GraphEvalContext& context, const String& pinName,
		                        const String& typeId, void* outValue) override
		{
			if (pinName != "Value" || !Parameter) return false;
			return Parameter->CopyValueTo(outValue, typeId);
		}
	};
}

#endif //PLUENGINE_PARTICLEPARAMETERNODE_H
