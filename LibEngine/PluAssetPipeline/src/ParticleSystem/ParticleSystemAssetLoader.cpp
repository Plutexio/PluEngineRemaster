//
// Created by Plutex on 9/25/26.
//

#include "PluEngine/AssetPipeline/ParticleSystem/ParticleSystemAssetLoader.h"

#include "PluEngine/Effects/Particles/ParticleSystem.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Core/Reflection/ReflectionBase.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/AssetTypes/NodeGraph/NodeGraphSerializer.h"

Plu::String Plu::ParticleSystemAssetLoader::GetSupportedAssetType()
{
	return ParticleSystem::GetStaticClass()->TypeName;
}

bool Plu::ParticleSystemAssetLoader::LoadAssetData(TUsePointer<AssetDescriptor> assetDesc,
	TOwningPointer<IAssetData> *assetDataToPopulate, TUsePointer<EngineAssetManager> assetManager,
	TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager,
	TUsePointer<IShaderManager> shaderManager)
{
	std::optional<JSON> jsonOpt = DiskManager::LoadJson(assetDesc->AssetPath.ToString().ToWide());
	if (!jsonOpt.has_value()) {
		PLU_CORE_ERROR("Failed to read ParticleSystem JSON at {}", assetDesc->AssetPath.ToString().CStr());
		return false;
	}

	DeserializationContext dc;
	dc.assetManager  = assetManager;
	dc.scenesManager = sceneManager;
	dc.shaderManager = shaderManager;

	auto* system = static_cast<ParticleSystem*>(ParticleSystem::GetStaticClass()->Construct());
	if (!system) return false;
	TypeSerializer<TypeInfo*>::Deserialize(&dc, jsonOpt.value(), ParticleSystem::GetStaticClass(), system);

	// Parameters first: they need the factory populated (ParticleParameterFactory::RegisterBuiltInTypes,
	// called from Application::EngineInit for Editor and Runtime alike).
	system->Parameters.Clear();
	if (jsonOpt->contains("parameters")) {
		for (const JSON& parameterJson : jsonOpt.value()["parameters"]) {
			String typeName = parameterJson.value("typeName", std::string()).c_str();
			TOwningPointer<IParticleParameter> parameter = ParticleParameterFactory::CreateParameter(typeName);
			if (!parameter) {
				PLU_CORE_WARN("ParticleSystem load: unknown parameter type '{}', skipping", typeName.CStr());
				continue;
			}
			parameter->Name = parameterJson.value("name", std::string()).c_str();
			if (parameterJson.contains("value"))
				parameter->DeSerialize(parameterJson["value"], &dc);
			system->Parameters.PushBack(parameter);
		}
	}

	system->Emitters.Clear();
	if (jsonOpt->contains("emitters")) {
		for (const JSON& emitterJson : jsonOpt.value()["emitters"]) {
			auto* emitter = static_cast<ParticleEmitter*>(ParticleEmitter::GetStaticClass()->Construct());
			if (!emitter) continue;
			TypeSerializer<TypeInfo*>::Deserialize(&dc, emitterJson, ParticleEmitter::GetStaticClass(), emitter);
			NodeGraphSerializer::Load(&dc, *emitter, emitterJson);
			system->Emitters.PushBack(TOwningPointer<ParticleEmitter>(emitter));
		}
	}

	// Nodes were built before the parameter list existed — bind the parameter nodes now, then drop
	// links that no longer type-check against the rebuilt pins.
	system->ResolveParameterReferences();
	for (TOwningPointer<ParticleEmitter>& emitter : system->Emitters)
		if (emitter) emitter->PruneInvalidLinks();

	*assetDataToPopulate = TOwningPointer(static_cast<IAssetData*>(system));
	return true;
}

#ifdef PLU_ENGINE_EDITOR_BUILD
Plu::TypeInfo * Plu::ParticleSystemAssetLoader::GetAssetTypeViewportClass()
{
	// Editor-side class, resolved by name so LibEngine stays free of an Editor include.
	return TypeRegistry::GetInstance()->GetTypeOfName("ParticleSystemViewport");
}

bool Plu::ParticleSystemAssetLoader::DispatchAssetSave(TUsePointer<AssetDescriptor> assetDesc,
	TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager,
	TUsePointer<SceneManager> sceneManager, TUsePointer<IShaderManager> shaderManager)
{
	TUsePointer<IAssetData> data = assetManager->GetAssetData(assetDesc);
	auto* system = dynamic_cast<ParticleSystem*>(data.GetRaw());
	if (!system) {
		PLU_CORE_ERROR("ParticleSystem save: asset data is not a ParticleSystem");
		return false;
	}

	// Persist parameter-node references by the current parameter name (it may have been renamed).
	system->SyncParameterNodeNames();

	JSON json = TypeSerializer<TypeInfo*>::Serialize(assetDesc->AssetType, system);

	json["parameters"] = JSON::array();
	for (TOwningPointer<IParticleParameter>& parameter : system->Parameters) {
		if (!parameter) continue;
		JSON parameterJson;
		parameterJson["name"]     = parameter->Name.CStr();
		parameterJson["typeName"] = parameter->TypeName.CStr();
		parameterJson["value"]    = parameter->Serialize();
		json["parameters"].push_back(parameterJson);
	}

	json["emitters"] = JSON::array();
	for (TOwningPointer<ParticleEmitter>& emitter : system->Emitters) {
		if (!emitter) continue;
		JSON emitterJson = TypeSerializer<TypeInfo*>::Serialize(ParticleEmitter::GetStaticClass(), emitter.GetRaw());
		NodeGraphSerializer::Save(*emitter, emitterJson);
		json["emitters"].push_back(emitterJson);
	}

	json["uuid"] = system->Uuid.getUUID();
	DiskManager::SaveJson(assetDesc->AssetPath.ToString().ToWide(), json);
	return true;
}

bool Plu::ParticleSystemAssetLoader::IsAssetCreatable()
{
	return true;
}

bool Plu::ParticleSystemAssetLoader::CanImportAsset(Path assetPath, TUsePointer<EngineAssetManager> assetManager,
	TUsePointer<EngineObjectManager> objectManager)
{
	return false;
}
#endif
