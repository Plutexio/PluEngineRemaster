//
// Created by Plutex on 18.05.2026.
//

#ifndef PLUENGINE_ASSETDESCRIPTOR_H
#define PLUENGINE_ASSETDESCRIPTOR_H

#include "PluEngine/Core.h"
#include "AssetDescriptor.generated.h"
#include "PluBase/PluUUID.h"


namespace Plu
{
    PLU_ENUM(PyNamespace=Plu)
    enum class AssetLoaderType
    {
        JSON,
        Binary,
        Undefined
    };

    PLU_STRUCT()
    struct PLUASSETCORE_API AssetDescriptor
    {
        REFLECTION_BODY_ASSETDESCRIPTOR()
    public:

        PluUUID Uuid;
        TypeInfo* AssetType;
        AssetLoaderType LoaderType;
        Path AssetPath;

#ifdef PLU_ENGINE_EDITOR_BUILD
        String AssetName;
        bool IsEngineAsset;
#endif
    };

    // An asset file's own description of itself, read off disk on any thread. Plain data:
    // EngineAssetManager turns it into an AssetDescriptor on the main thread.
    struct AssetDescriptorHeader
    {
        Path AssetPath;
        String TypeName;
        UInt64 Uuid = 0;
        AssetLoaderType LoaderType = AssetLoaderType::Undefined;
#ifdef PLU_ENGINE_EDITOR_BUILD
        String AssetName;
#endif
        bool Valid = false;
    };
}

#endif //PLUENGINE_ASSETDESCRIPTOR_H
