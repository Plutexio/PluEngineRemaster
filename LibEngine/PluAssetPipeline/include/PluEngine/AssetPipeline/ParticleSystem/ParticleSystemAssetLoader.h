//
// Created by Plutex on 9/25/26.
//

#ifndef PLUENGINE_PARTICLESYSTEMASSETLOADER_H
#define PLUENGINE_PARTICLESYSTEMASSETLOADER_H
#include "PluEngine/Core.h"
#include "PluEngine/AssetCore/AssetLoader.h"
#include "ParticleSystemAssetLoader.generated.h"

namespace Plu
{
	// Loader for ParticleSystem assets (.pluasset, JSON, typeName "ParticleSystem"). Authored in the
	// editor, so no import extensions. Format: the system's own reflected fields, "parameters"[] and
	// "emitters"[] (each a ParticleEmitter's fields + NodeGraphSerializer nodes/links).
	PLU_CLASS()
	class PLUASSETPIPELINE_API ParticleSystemAssetLoader : public IAssetLoader
	{
		REFLECTION_BODY_PARTICLESYSTEMASSETLOADER()
	public:
		ParticleSystemAssetLoader() = default;
		virtual ~ParticleSystemAssetLoader() override = default;

		String GetSupportedAssetType() override;
		bool LoadAssetData(TUsePointer<AssetDescriptor> assetDesc, TOwningPointer<IAssetData> *assetDataToPopulate,
		                   TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager,
		                   TUsePointer<SceneManager> sceneManager,
		                   TUsePointer<IShaderManager> shaderManager) override;

#ifdef PLU_ENGINE_EDITOR_BUILD
		TypeInfo *GetAssetTypeViewportClass() override;
		bool DispatchAssetSave(TUsePointer<AssetDescriptor> assetDesc, TUsePointer<EngineAssetManager> assetManager,
		                       TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager,
		                       TUsePointer<IShaderManager> shaderManager) override;
		bool IsAssetCreatable() override;
		bool CanImportAsset(Path assetPath, TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager) override;
#endif
	};
}

#endif //PLUENGINE_PARTICLESYSTEMASSETLOADER_H
