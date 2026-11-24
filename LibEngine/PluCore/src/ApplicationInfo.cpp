//
// Created by Plutex on 12/30/25.
//

#include "PluEngine/Core/ApplicationInfo.h"

#include "PluEngine/Core/Reflection/ReflectionBase.h"

namespace Plu
{
    DeserializationContext * ApplicationInfo::ConstructDeserializationContext() const
    {
        DeserializationContext* context = new DeserializationContext();
        context->assetManager = AppAssetManager;
        context->scenesManager = AppScenesManager;
        return context;
    }
}
