//
// Created by Plutex on 8/12/26.
//

#ifndef PLUENGINE_SHADERPROGRAMINFO_H
#define PLUENGINE_SHADERPROGRAMINFO_H
#include "PluEngine/Core.h"
#include "PluEngine/Core/IAssetData.h"
#include "PluBase/PluUUID.h"
#include "ShaderProgramInfo.generated.h"

namespace Plu
{
    // The asset side of a shader program: which two shader code assets it is built from. Separate
    // from ShaderProgram, which is the live GL object — this is data, and lives with the other
    // asset types so that Material can name it without the asset layer reaching into the renderer.
    PLU_STRUCT()
    struct PLUASSETTYPES_API ShaderProgramInfo : IAssetData
    {
        REFLECTION_BODY_SHADERPROGRAMINFO()

        PLU_PROPERTY(UuidFor=IShaderCode)
        PluUUID VertexShaderUuid;

        PLU_PROPERTY(UuidFor=IShaderCode)
        PluUUID FragmentShaderUuid;
    };
}

#endif //PLUENGINE_SHADERPROGRAMINFO_H
