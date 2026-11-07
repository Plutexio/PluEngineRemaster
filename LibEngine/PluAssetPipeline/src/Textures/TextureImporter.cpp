//
// Created by Plutex on 2026-02-07.
//

#include "PluEngine/AssetPipeline/Textures/TextureImporter.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "PluBase/Log.h"

namespace Plu
{
namespace
{
	// Zapis zdekodowanych pikseli do formatu binarnego TextureInfo (PLUA).
	// Wspólny dla importu z pliku i z pamięci. Zwalnia `data` (stbi_image_free).
	bool WriteTextureAsset(unsigned char* data, int width, int height, int channels, const PathW& outPath)
	{
		FILE* file = nullptr;

	#ifdef _WIN32
		_wfopen_s(&file, outPath.CStr(), L"wb");
	#else
		file = fopen(String::FromWide(outPath.CStr()).CStr(), "wb");
	#endif

		if (!file)
		{
			PLU_ERROR("Failed to open file for writing: {}", String::FromWide(outPath.CStr()).CStr());
			stbi_image_free(data);
			return false;
		}

		// Magic number i wersja
		UInt32 magic = 0x41554C50;  // 'PLUA'
		UInt32 version = 1;
		fwrite(&magic, sizeof(UInt32), 1, file);
		fwrite(&version, sizeof(UInt32), 1, file);

		// Typ assetu
		const char* typeName = "TextureInfo";
		UInt32 typeLength = static_cast<UInt32>(strlen(typeName));
		fwrite(&typeLength, sizeof(UInt32), 1, file);
		fwrite(typeName, sizeof(char), typeLength, file);

		UInt64 uuid = PluUUID();
		fwrite(&uuid,sizeof(UInt64),1,file);

		UInt64 pixelCount = static_cast<UInt64>(width) * height * channels;
		fwrite(&width, sizeof(int), 1, file);
		fwrite(&height, sizeof(int), 1, file);
		fwrite(&channels, sizeof(int), 1, file);

		fwrite(data, sizeof(unsigned char), pixelCount, file);
		fclose(file);
		stbi_image_free(data);
		return true;
	}
}
}

bool Plu::TextureImport::ImportTexture(const PathW& origin, const PathW &outPath)
{
	int width = 0, height = 0, channels = 0;

	// Wczytaj piksele (RGBA wymuszone)
	stbi_set_flip_vertically_on_load(true);
	unsigned char* data = stbi_load(origin.ToString().ToNarrow().CStr(),
									&width, &height, &channels, 0);

	if (!data) {
		PLU_ERROR("Error importing texture!");
		return false;
	}

	return WriteTextureAsset(data, width, height, channels, outPath);
}

bool Plu::TextureImport::ImportTextureFromMemory(const unsigned char* compressedData, UInt64 size, const PathW& outPath)
{
	int width = 0, height = 0, channels = 0;

	// Dekoduj skompresowany blob (PNG/JPG/...) z pamięci — spójnie z importem z pliku (flip Y).
	stbi_set_flip_vertically_on_load(true);
	unsigned char* data = stbi_load_from_memory(compressedData, static_cast<int>(size),
												&width, &height, &channels, 0);

	if (!data) {
		PLU_ERROR("Error decoding embedded texture from memory! ({})", stbi_failure_reason());
		return false;
	}

	return WriteTextureAsset(data, width, height, channels, outPath);
}

void Plu::TextureImport::LoadTexture(const PathW &textPath, TextureInfo *textureInfo)
{
	FILE* file = nullptr;

#ifdef _WIN32
	_wfopen_s(&file, textPath.CStr(), L"rb");
#else
	file = fopen(String::FromWide(textPath.CStr()).CStr(), "rb");
#endif

	if (!file)
	{
		PLU_CORE_ERROR("Failed to open file: {}", String::FromWide(textPath.CStr()).CStr());
		return;
	}

	// Sprawdź magic number i wersję
	UInt32 magic = 0;
	UInt32 version = 0;
	fread(&magic, sizeof(UInt32), 1, file);
	fread(&version, sizeof(UInt32), 1, file);

	if (magic != 0x41554C50 || version != 1)
	{
		PLU_ERROR("File {} has invalid magic or version!", String::FromWide(textPath.CStr()).CStr());
		fclose(file);
		return;
	}

	// Typ assetu
	UInt32 typeLength = 0;
	fread(&typeLength, sizeof(UInt32), 1, file);
	char* typeBuffer = new char[typeLength + 1];
	fread(typeBuffer, sizeof(char), typeLength, file);
	typeBuffer[typeLength] = '\0';

	if (strcmp(typeBuffer, "TextureInfo") != 0)
	{
		PLU_ERROR("File {} is not a TextureInfo!", String::FromWide(textPath.CStr()).CStr());
		delete[] typeBuffer;
		fclose(file);
		return;
	}
	delete[] typeBuffer;

	UInt64 uuid;
	fread(&uuid, sizeof(UInt64), 1, file);
	textureInfo->Uuid = uuid;

	// UInt64 pixelCount = width * height * channels;
	// fwrite(&width, sizeof(UInt32), 1, file);
	// fwrite(&height, sizeof(UInt32), 1, file);
	// fwrite(&channels, sizeof(UInt32), 1, file);
	//
	// fwrite(data, sizeof(unsigned char), pixelCount, file);
	// fclose(file);
	int width, height, channels;
	fread(&width, sizeof(int), 1, file);
	fread(&height, sizeof(int), 1, file);
	fread(&channels, sizeof(int), 1, file);
	UInt64 pixelCount = width * height * channels;
	PLU_INFO("Loading texture: W {}, H{}, C{}, Pixels {}", width, height, channels, pixelCount);
	unsigned char* data = new unsigned char[pixelCount];
	fread(data, sizeof(unsigned char), pixelCount, file);
	fclose(file);
	textureInfo->Channels = channels;
	textureInfo->Width = width;
	textureInfo->Height = height;
	textureInfo->Data = data;
	textureInfo->Uuid = uuid;
}
