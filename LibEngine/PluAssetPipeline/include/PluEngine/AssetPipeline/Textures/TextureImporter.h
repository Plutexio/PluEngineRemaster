//
// Created by Plutex on 2026-02-07.
//

#ifndef PLUENGINE_TEXTUREIMPORTER_H
#define PLUENGINE_TEXTUREIMPORTER_H
#include "PluSTL/Path/Path.h"
#include "PluEngine/AssetTypes/Image/ImageInfo.h"

namespace Plu
{
	namespace TextureImport
	{
		// Decodes a source image (PNG, JPG, ...) and writes it as an ImageInfo .plubin.
		bool ImportTexture(const PathW& origin, const PathW &outPath);
		// Import tekstury z bufora w pamięci (np. embedded w modelu) — skompresowany blob (PNG/JPG/...).
		bool ImportTextureFromMemory(const unsigned char* compressedData, UInt64 size, const PathW& outPath);
		// Reads an ImageInfo .plubin; null when the file is missing or not an ImageInfo.
		TOwningPointer<ImageInfo> LoadTexture(const PathW &textPath);
	}
}

#endif //PLUENGINE_TEXTUREIMPORTER_H
