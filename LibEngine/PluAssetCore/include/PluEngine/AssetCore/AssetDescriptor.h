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
}

#endif //PLUENGINE_ASSETDESCRIPTOR_H
