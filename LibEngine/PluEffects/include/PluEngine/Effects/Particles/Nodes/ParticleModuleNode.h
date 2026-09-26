//
// Created by Plutex on 9/25/26.
//

#ifndef PLUENGINE_PARTICLEMODULENODE_H
#define PLUENGINE_PARTICLEMODULENODE_H

#include "PluEngine/AssetTypes/NodeGraph/GraphNode.h"
#include "PluEngine/Effects/Particles/ParticleAttributes.h"
#include "ParticleModuleNode.generated.h"

namespace Plu
{
	struct ParticleCompileContext;

	// Base of every node in a ParticleEmitter graph. A module is chained to the next by a Flow pin of
	// kind "Particle"; ParticleEmitterOutputNode terminates the chain and the compiler walks it
	// backwards from there.
	PLU_STRUCT(Abstract)
	struct PLUEFFECTS_API ParticleModuleNode : GraphNode
	{
		REFLECTION_BODY_PARTICLEMODULENODE()

		static constexpr const char* ParticleFlow = "Particle";

		[[nodiscard]] virtual EParticleModuleStage GetStage() const { return EParticleModuleStage::Update; }

		// Emits this module's ops into the compiled emitter. Implemented together with the compiler.
		virtual void Compile(ParticleCompileContext& ctx) {}

	protected:
		void AddParticleInput()  { AddPin("In",  EPinDirection::Input,  EPinCategory::Flow, ParticleFlow); }
		void AddParticleOutput() { AddPin("Out", EPinDirection::Output, EPinCategory::Flow, ParticleFlow); }

		// Data input pin overriding the module's authored value for `name`. Explicit because
		// BuildDataPinsFromReflection() only pins float/bool/int/Vec2-4 properties, and a
		// ParticleParam* property is deliberately not one of them: the widget is the authored value,
		// the pin is the override.
		void AddParamPin(const String& name, const char* typeId)
		{
			AddPin(name, EPinDirection::Input, EPinCategory::Data, typeId);
		}
	};
}

#endif //PLUENGINE_PARTICLEMODULENODE_H
