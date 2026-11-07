//
// Created by Plutex on 9/25/26.
//

#include "PluEngine/Effects/Particles/ParticleSystem.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleEmitterOutputNode.h"
#include "PluEngine/Effects/Particles/Nodes/ParticleParameterNode.h"
// RegisterBuiltInTypes() instantiates ParticleParameter<T>::(De)Serialize/DrawEditorControl, whose
// bodies call TypeSerializer<T> — the primitive specialisations must be visible here.
#include "PluEngine/Core/Reflection/TypeTraits.h"

namespace Plu
{
	HashMap<String, ParticleParameterFactory::ParameterTypeInfo>& ParticleParameterFactory::GetFactoryMap()
	{
		static HashMap<String, ParameterTypeInfo> FactoryMap;
		return FactoryMap;
	}

	void ParticleParameterFactory::RegisterBuiltInTypes()
	{
		RegisterType<float>("Float", "float", Vec3(0, 255, 17));
		RegisterType<int>("Integer", "int", Vec3(0, 255, 140));
		RegisterType<bool>("Boolean", "bool", Vec3(163, 3, 0));
		RegisterType<Vec3>("Vec3", "Vec3", Vec3(255, 208, 0));
		RegisterType<Vec4>("Color", "Vec4", Vec3(255, 140, 200));
	}

	ParticleModuleNode* ParticleEmitter::FindOutputNode()
	{
		for (TOwningPointer<GraphNode>& node : Nodes) {
			if (auto* output = dynamic_cast<ParticleEmitterOutputNode*>(node.GetRaw())) return output;
		}
		return nullptr;
	}

	ParticleEmitter* ParticleSystem::FindEmitter(const PluUUID& uuid)
	{
		for (TOwningPointer<ParticleEmitter>& emitter : Emitters) {
			if (emitter && emitter->Uuid == uuid) return emitter.GetRaw();
		}
		return nullptr;
	}

	TUsePointer<IParticleParameter> ParticleSystem::FindParameter(const String& name)
	{
		for (TOwningPointer<IParticleParameter>& parameter : Parameters) {
			if (parameter && parameter->Name == name) return parameter;
		}
		return nullptr;
	}

	ParticleEmitter* ParticleSystem::AddEmitter(const String& name)
	{
		auto* emitter = static_cast<ParticleEmitter*>(ParticleEmitter::GetStaticClass()->Construct());
		if (!emitter) return nullptr;
		emitter->EmitterName = name;
		Emitters.PushBack(TOwningPointer<ParticleEmitter>(emitter));
		return emitter;
	}

	ParticleModuleNode* ParticleSystem::AddParameterNode(ParticleEmitter& emitter, const TUsePointer<IParticleParameter>& parameter)
	{
		if (!parameter) return nullptr;
		auto* node = static_cast<ParticleParameterNode*>(ParticleParameterNode::GetStaticClass()->Construct());
		if (!node) return nullptr;
		node->Parameter     = parameter;
		node->ParameterName = parameter->Name;
		node->BuildPins();
		emitter.Nodes.PushBack(TOwningPointer<GraphNode>(node));
		return node;
	}

	void ParticleSystem::ResolveParameterReferences()
	{
		for (TOwningPointer<ParticleEmitter>& emitter : Emitters) {
			if (!emitter) continue;
			for (TOwningPointer<GraphNode>& node : emitter->Nodes) {
				auto* parameterNode = dynamic_cast<ParticleParameterNode*>(node.GetRaw());
				if (!parameterNode) continue;
				parameterNode->Parameter = FindParameter(parameterNode->ParameterName);
				// Pins were built once during load with no bound parameter (untyped); clear first or
				// the pin would be duplicated.
				parameterNode->InputPins.Clear();
				parameterNode->OutputPins.Clear();
				parameterNode->BuildPins();
			}
		}
	}

	void ParticleSystem::SyncParameterNodeNames()
	{
		for (TOwningPointer<ParticleEmitter>& emitter : Emitters) {
			if (!emitter) continue;
			for (TOwningPointer<GraphNode>& node : emitter->Nodes) {
				auto* parameterNode = dynamic_cast<ParticleParameterNode*>(node.GetRaw());
				if (parameterNode && parameterNode->Parameter) parameterNode->ParameterName = parameterNode->Parameter->Name;
			}
		}
	}
}
