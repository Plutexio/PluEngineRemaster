//
// Created by Plutex on 2026-02-07.
//

#ifndef PLUENGINE_TEXTURE_H
#define PLUENGINE_TEXTURE_H
#include "PluEngine/Core.h"
#include "PluEngine/Core/IAssetData.h"
#include "Texture.generated.h"

namespace Plu
{
	PLU_STRUCT()
	struct PLUASSETTYPES_API TextureRawData
	{
		REFLECTION_BODY_TEXTURERAWDATA()

		~TextureRawData() { delete[] Data; }

		PLU_PROPERTY()
		Int32 Width = 0;

		PLU_PROPERTY()
		Int32 Height = 0;

		PLU_PROPERTY()
		Int32 Channels = 0;

		unsigned char* Data = nullptr;
	};

	PLU_STRUCT()
	struct PLUASSETTYPES_API TextureInfo : IAssetData
	{
		REFLECTION_BODY_TEXTUREINFO()


		TextureRawData RawData;
	};
}

#endif //PLUENGINE_TEXTURE_H
