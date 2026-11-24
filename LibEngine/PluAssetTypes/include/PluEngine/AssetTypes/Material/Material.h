//
// Created by Plutex on 25.01.2026.
//

#ifndef PLUENGINE_MATERIAL_H
#define PLUENGINE_MATERIAL_H

#include "PluEngine/Core.h"
#include "PluEngine/Core/IAssetData.h"
#include "Material.generated.h"

namespace Plu
{
    PLU_STRUCT(PyExport)
    struct PLUASSETTYPES_API MaterialInfo : IAssetData
    {
        REFLECTION_BODY_MATERIALINFO()
        // Empty until the Vulkan material model exists: the GLSL shader program reference and its
        // uniform parameters went away with the OpenGL renderer.
    };
}

#endif //PLUENGINE_MATERIAL_H
