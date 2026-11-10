//
// Created by Plutex on 7/5/26.
//

#ifndef PLUENGINE_SKELETALMESHASSETLOADER_H
#define PLUENGINE_SKELETALMESHASSETLOADER_H
#include "PluEngine/Core.h"
#include "PluEngine/AssetCore/AssetLoader.h"
#include "SkeletalMeshAssetLoader.generated.h"
#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"

namespace Plu
{

    PLU_STRUCT()
    struct SkeletalMeshImportOptions
    {
        REFLECTION_BODY_SKELETALMESHIMPORTOPTIONS()

        // Merge all skinned meshes that resolve to the same skeleton into one asset
        // (one merged mesh per skeleton found in the file).
        PLU_PROPERTY()
        bool Merge = true;

        // Uniform scale baked into the imported geometry, skeleton and animation translations —
        // "multiply this source file's units by X to get engine units". Stored on the resulting
        // skeleton as Skeleton::ImportScale. See UseSkeletonImportScale for what happens when it
        // disagrees with the skeleton being imported against.
        PLU_PROPERTY()
        float Scale = 1.0f;

        // When importing against an existing rig (SkeletonToUse), take the scale from that rig's
        // Skeleton::ImportScale instead of the Scale field above. On by default: it makes a
        // mistyped Scale harmless, since a mesh or clip can then never land in a different space
        // than the skeleton driving it.
        //
        // Turn it OFF when the source file genuinely has different units than the file the rig
        // came from (e.g. the rig was exported in centimetres, this clip in metres) — then Scale
        // is the file's own unit conversion and forcing the rig's value would be wrong. The
        // import log always says which value was used.
        PLU_PROPERTY()
        bool UseSkeletonImportScale = true;

        // Name imported animation assets after the clip's own name in the source file (Blender
        // writes the action name, e.g. "Armature|ArmatureAction" -> "ArmatureAction") rather
        // than after the source file. Clips whose name carries no information — unnamed ones,
        // or an exporter's boilerplate stamp such as Mixamo's "mixamo.com" — still fall back to
        // the file name, so this is safe to leave on for files that have nothing to say.
        PLU_PROPERTY()
        bool UseClipNamesForAnimations = true;

        // When a file's node hierarchy disagrees with its own skinning matrices, rebuild the
        // skeleton's bind pose from the latter (they are what the mesh was actually skinned
        // against) instead of trusting the hierarchy.
        //
        // Some FBX files come out of Assimp half-converted: the axis conversion it reports in the
        // scene metadata reaches the animation curves but not the vertices, the inverse-bind
        // matrices or the node translations, which leaves the rest pose visibly broken while the
        // animations play correctly. The importer only acts when it measures a real disagreement,
        // so healthy files are untouched — turn this off to import a file exactly as Assimp hands
        // it over.
        PLU_PROPERTY()
        bool RebuildBindPoseFromSkinning = true;

        PLU_PROPERTY()
        bool FlipUVs = true;

        PLU_PROPERTY()
        bool GenerateNormals = false;

        PLU_PROPERTY()
        TUsePointer<Skeleton> SkeletonToUse;
    };

    PLU_CLASS()
    class PLUASSETPIPELINE_API SkeletalMeshAssetLoader : public IAssetLoader
    {
        REFLECTION_BODY_SKELETALMESHASSETLOADER()
    public:
        SkeletalMeshAssetLoader() = default;
        virtual ~SkeletalMeshAssetLoader() override = default;

        String GetSupportedAssetType() override;
        bool LoadAssetData(TUsePointer<AssetDescriptor> assetDesc, TOwningPointer<IAssetData> *assetDataToPopulate,
                           TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager,
                           TUsePointer<SceneManager> sceneManager,
                           TUsePointer<IShaderManager> shaderManager) override;

#ifdef PLU_ENGINE_EDITOR_BUILD
        DynamicArray<String> GetSupportedImportExtensions() override;
        TypeInfo *GetImportSettingsClass() override;
        void HandleAssetImporting(Path &assetPath, Path outPath, void *importSettings, TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager) override;
        TypeInfo *GetAssetTypeViewportClass() override;
        bool DispatchAssetSave(TUsePointer<AssetDescriptor> assetDesc, TUsePointer<EngineAssetManager> assetManager,
                               TUsePointer<EngineObjectManager> objectManager, TUsePointer<SceneManager> sceneManager,
                               TUsePointer<IShaderManager> shaderManager) override;
        bool IsAssetCreatable() override;
        bool CanImportAsset(Path assetPath, TUsePointer<EngineAssetManager> assetManager, TUsePointer<EngineObjectManager> objectManager) override;
#endif
    };
}

#endif //PLUENGINE_SKELETALMESHASSETLOADER_H
