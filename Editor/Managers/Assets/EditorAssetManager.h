//
// Created by Plutex on 1/4/26.
//

#ifndef PLUENGINE_EDITORASSETMANAGER_H
#define PLUENGINE_EDITORASSETMANAGER_H
#include "PluEngine/Core/IAssetData.h"
#include "PluSTL/PluSTL_FWD.h"

namespace Plu
{
	class EngineObjectManager;
	class IEditorAssetObject;
	class EditorProjectManager;

	class EditorTypeRegistry
	{
	public:
		using EditorAssetConstructor = std::function<TOwningPointer<IEditorAssetObject>(TOwningPointer<IAssetData>)>;
	private:
		HashMap<String, EditorAssetConstructor> mEditorAssetsCreators;
	public:
		static EditorTypeRegistry* GetInstance();
		void AddConstructor(String name, EditorAssetConstructor cons);
		TOwningPointer<IEditorAssetObject> ConstructAssetObject(TypeInfo* type, const TOwningPointer<IAssetData> &assetInfo);
	};
}

#endif //PLUENGINE_EDITORASSETMANAGER_H