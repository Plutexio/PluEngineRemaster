//
// Created by Plutex on 7/20/26.
//

#include "PluEngine/AssetPipeline/AnimationGraph/AnimationGraphAssetLoader.h"

#include "PluEngine/AssetTypes/AnimationGraph/AnimationGraph.h"
#include "PluEngine/AssetCore/AssetDescriptor.h"
#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Core/Reflection/ReflectionBase.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "PluEngine/AssetTypes/NodeGraph/NodeGraphSerializer.h"

Plu::String Plu::AnimationGraphAssetLoader::GetSupportedAssetType()
{
    return AnimationGraph::GetStaticClass()->TypeName;
}

bool Plu::AnimationGraphAssetLoader::LoadAssetData(TUsePointer<AssetDescriptor> assetDesc,
    TOwningPointer<IAssetData> *assetDataToPopulate, TUsePointer<EngineAssetManager> assetManager,
    TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager)
{
    // Registering a loader takes this type off EngineAssetManager's generic JSON path. The graph's
    // own reflected fields ride TypeSerializer<TypeInfo*>, but the polymorphic node list + links need
    // the hand-written NodeGraphSerializer (the generic array serializer drops subclass identity).
    std::optional<JSON> jsonOpt = DiskManager::LoadJson(assetDesc->AssetPath.ToString().ToWide());
    if (!jsonOpt.has_value()) {
        PLU_CORE_ERROR("Failed to read AnimationGraph JSON at {}", assetDesc->AssetPath.ToString().CStr());
        return false;
    }

    DeserializationContext dc;
    dc.assetManager  = assetManager;
    dc.scenesManager = sceneManager;

    auto* graph = static_cast<AnimationGraph*>(AnimationGraph::GetStaticClass()->Construct());
    if (!graph) return false;
    // Reflected fields on the graph itself (Uuid, plus any future PLU_PROPERTY).
    TypeSerializer<TypeInfo*>::Deserialize(&dc, jsonOpt.value(), AnimationGraph::GetStaticClass(), graph);
    // Polymorphic nodes + links.
    NodeGraphSerializer::Load(&dc, *graph, jsonOpt.value());

    // Variables are anim-specific (not part of the generic node graph). Each is rebuilt from its
    // stored typeName via the factory, then its value is deserialized into the concrete instance.
    // Needs the factory populated (AnimationGraphVariableFactory::RegisterBuiltInTypes, called from
    // Application::EngineInit for both Editor and Runtime) — an unknown typeName here means a custom
    // variable type that was never registered, not a build-config gap.
    graph->Variables.Clear();
    if (jsonOpt->contains("variables")) {
        for (const JSON& variableJson : jsonOpt.value()["variables"]) {
            String typeName = variableJson.value("typeName", std::string()).c_str();
            TOwningPointer<IAnimationGraphVariable> variable = AnimationGraphVariableFactory::CreateVariable(typeName);
            if (!variable) {
                PLU_CORE_WARN("AnimationGraph load: unknown variable type '{}', skipping", typeName.CStr());
                continue;
            }
            variable->Name = variableJson.value("name", std::string()).c_str();
            if (variableJson.contains("value"))
                variable->DeSerialize(variableJson["value"], &dc);
            graph->Variables.PushBack(variable);
        }
    }
    // Nodes loaded before variables, so their variable references are still unresolved — bind them
    // now that the variable list exists (also rebuilds each variable node's pins with the right type).
    graph->ResolveVariableReferences();
    // Pin sets are rebuilt from node properties on load, so a link saved against a pin that no longer
    // exists (or no longer type-checks — a value node saved with a different ValueType, a hand-edited
    // file) would otherwise stay in the graph and be followed during evaluation.
    graph->PruneInvalidLinks();

    *assetDataToPopulate = TOwningPointer(static_cast<IAssetData*>(graph));
    return true;
}

#ifdef PLU_ENGINE_EDITOR_BUILD
Plu::TypeInfo * Plu::AnimationGraphAssetLoader::GetAssetTypeViewportClass()
{
    // Editor-side class, resolved by name so LibEngine stays free of an Editor include.
    return TypeRegistry::GetInstance()->GetTypeOfName("AnimationGraphViewport");
}

bool Plu::AnimationGraphAssetLoader::DispatchAssetSave(TUsePointer<AssetDescriptor> assetDesc,
    TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager,
    TUsePointer<SceneManager> sceneManager)
{
    // The graph owns a polymorphic node list, which the generic reflection serializer can't handle
    // (it would drop each node's concrete type). So we write the file ourselves: reflected fields via
    // TypeSerializer<TypeInfo*>, then nodes + links via NodeGraphSerializer. Returning true tells
    // EngineAssetManager we've persisted it, so it does not fall through to the generic path.
    TUsePointer<IAssetData> data = assetManager->GetAssetData(assetDesc);
    auto* graph = dynamic_cast<NodeGraph*>(data.GetRaw());
    if (!graph) {
        PLU_CORE_ERROR("AnimationGraph save: asset data is not a NodeGraph");
        return false;
    }

    // Persist each variable node's reference by the current variable name (it may have been renamed
    // since load) before the generic node serializer writes VariableName out.
    if (auto* animGraphForSync = dynamic_cast<AnimationGraph*>(data.GetRaw())) {
        animGraphForSync->SyncVariableNodeNames();
    }

    JSON json = TypeSerializer<TypeInfo*>::Serialize(assetDesc->AssetType, data.GetRaw());
    NodeGraphSerializer::Save(*graph, json);

    // Variables: name + concrete typeName (for the factory on load) + the value's own JSON.
    json["variables"] = JSON::array();
    if (auto* animGraph = dynamic_cast<AnimationGraph*>(data.GetRaw())) {
        for (TOwningPointer<IAnimationGraphVariable>& variable : animGraph->Variables) {
            if (!variable) continue;
            JSON variableJson;
            variableJson["name"]     = variable->Name.CStr();
            variableJson["typeName"] = variable->TypeName.CStr();
            variableJson["value"]    = variable->Serialize();
            json["variables"].push_back(variableJson);
        }
    }

    json["uuid"] = graph->Uuid.getUUID();
    DiskManager::SaveJson(assetDesc->AssetPath.ToString().ToWide(), json);
    return true;
}

bool Plu::AnimationGraphAssetLoader::IsAssetCreatable()
{
    // Shows up under "Create Asset" in the asset browser.
    return true;
}

bool Plu::AnimationGraphAssetLoader::CanImportAsset(Path assetPath, TUsePointer<EngineAssetManager> assetManager,
    TUsePointer<EngineObjectManager> objectManager)
{
    // Authored in the editor, never imported from a source file.
    return false;
}
#endif
