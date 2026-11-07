//
// Created by Plutex on 2026-02-07.
//

#ifndef PLUENGINE_TEXTUREIMPORTER_H
#define PLUENGINE_TEXTUREIMPORTER_H
#include "PluSTL/Path/Path.h"
#include "PluEngine/AssetTypes/Texture/Texture.h"

namespace Plu
{
	namespace TextureImport
	{
		bool ImportTexture(const PathW& origin, const PathW &outPath);
		// Import tekstury z bufora w pamięci (np. embedded w modelu) — skompresowany blob (PNG/JPG/...).
		bool ImportTextureFromMemory(const unsigned char* compressedData, UInt64 size, const PathW& outPath);
		void LoadTexture(const PathW &textPath, TextureInfo* textureInfo);
	}
}

#endif //PLUENGINE_TEXTUREIMPORTER_H