//
// Created by Plutex on 1/4/26.
//

#include "EditorAssetManager.h"

#include <utility>
#include "Managers/Project/EditorProjectManager.h"
#include "PluBase/PluUUID.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"

Plu::EditorTypeRegistry * Plu::EditorTypeRegistry::GetInstance()
{
    static EditorTypeRegistry* editorTypeRegistry;
    if (!editorTypeRegistry) {
        editorTypeRegistry = new EditorTypeRegistry();
    }
    return editorTypeRegistry;
}

void Plu::EditorTypeRegistry::AddConstructor(String name, EditorTypeRegistry::EditorAssetConstructor cons)
{
    mEditorAssetsCreators[name] = std::move(cons);
}

Plu::TOwningPointer<Plu::IEditorAssetObject> Plu::EditorTypeRegistry::ConstructAssetObject(TypeInfo *type, const TOwningPointer<IAssetData> &assetInfo)
{
    if (!type) return nullptr;
    return mEditorAssetsCreators.Find(type->TypeName)->operator()(assetInfo);
}
