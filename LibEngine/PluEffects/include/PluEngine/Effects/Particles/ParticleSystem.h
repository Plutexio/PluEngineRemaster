//
// Created by Plutex on 9/25/26.
//

#ifndef PLUENGINE_PARTICLESYSTEM_H
#define PLUENGINE_PARTICLESYSTEM_H

#include "PluEngine/Core.h"
#include "PluEngine/Core/IAssetData.h"
#include "PluEngine/AssetTypes/NodeGraph/NodeGraph.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleModuleNode.h"
#include "PluEngine/Effects/Particles/ParticleParameter.h"
#include "Array/Array.h"
#include "Pointers/TOwningPointer.h"
#include "ParticleSystem.generated.h"

namespace Plu
{
	// One emitter == one node graph. Reuses NodeGraph's asset identity (Uuid is never registered with
	// the asset manager for a sub-object), so NodeGraphEditor / NodeGraphSerializer work on it as is.
	PLU_STRUCT()
	struct PLUEFFECTS_API ParticleEmitter : NodeGraph
	{
		REFLECTION_BODY_PARTICLEEMITTER()

		PLU_PROPERTY()
		String EmitterName = "Emitter";
		PLU_PROPERTY()
		bool Enabled = true;
		PLU_PROPERTY()
		UInt32 MaxParticles = 65536;
		PLU_PROPERTY()
		bool Loop = true;
		// Seconds; 0 = never ends.
		PLU_PROPERTY()
		float Duration = 1.0f;

		TypeInfo* GetNodeBaseType() override { return ParticleModuleNode::GetStaticClass(); }

		// The terminator node of the module chain, or null when the graph has none.
		[[nodiscard]] ParticleModuleNode* FindOutputNode();
	};

	// A reusable VFX asset: N emitters sharing a set of user parameters. It owns its graphs rather than
	// being one, so an asset can hold Flash + Smoke + Sparks.
	PLU_STRUCT()
	struct PLUEFFECTS_API ParticleSystem : IAssetData
	{
		REFLECTION_BODY_PARTICLESYSTEM()

		// 0 = derive bounds from the simulation.
		PLU_PROPERTY()
		float FixedBoundsRadius = 0.0f;

		// Not PLU_PROPERTY (like NodeGraph::Nodes): polymorphic, saved by ParticleSystemAssetLoader.
		DynamicArray<TOwningPointer<ParticleEmitter>> Emitters;
		DynamicArray<TOwningPointer<IParticleParameter>> Parameters;

		// Never serialized. Bump on every edit that changes what the executor runs (any graph or
		// parameter-layout change): the compiler recompiles, the render thread re-adopts the program.
		UInt32 CompileRevision = 0;
		// Bump only when the parameter LAYOUT changes (add/remove/rename/retype) — that also bumps
		// CompileRevision, since operand offsets are baked into the ops. Editing a parameter's VALUE
		// bumps neither.
		UInt32 ParametersRevision = 0;

		[[nodiscard]] ParticleEmitter* FindEmitter(const PluUUID& uuid);
		[[nodiscard]] TUsePointer<IParticleParameter> FindParameter(const String& name);

		// Appends a new emitter with the given name; returns a non-owning view.
		ParticleEmitter* AddEmitter(const String& name);

		// Constructs a ParticleParameterNode bound to `parameter` in `emitter`, builds pins and appends it.
		ParticleModuleNode* AddParameterNode(ParticleEmitter& emitter, const TUsePointer<IParticleParameter>& parameter);

		// Binds every ParticleParameterNode's live Parameter from its serialized ParameterName and
		// rebuilds its pins. Run after Parameters are loaded (they load after the graphs).
		void ResolveParameterReferences();
		// Writes each bound node's ParameterName back from its live parameter (rename-safe). Run before save.
		void SyncParameterNodeNames();
	};
}

#endif //PLUENGINE_PARTICLESYSTEM_H
