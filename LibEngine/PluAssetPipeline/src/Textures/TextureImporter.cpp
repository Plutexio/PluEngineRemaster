//
// Created by Plutex on 2026-02-07.
//

#include "PluEngine/AssetPipeline/Textures/TextureImporter.h"

#include "PluBase/Log.h"
#include "PluEngine/Timer.h"

namespace Plu
{
namespace
{
	constexpr UInt32 kAssetMagic = 0x41554C50; // 'PLUA'
	constexpr UInt32 kImageAssetVersion = 1;
	constexpr const char* kImageAssetTypeName = "ImageInfo";

	// ImageInfo .plubin layout, after the common PLUA header (magic, version, type name, uuid):
	//   UInt32 width, UInt32 height, UInt32 channels, UInt8 grayscale, UInt8 hasAlpha,
	//   width * height * channels bytes of pixels, top row first.
	bool WriteImageAsset(const ImageInfo& image, const PathW& outPath)
	{
		PLU_PROFILE_SCOPE("TextureImport::WriteImageAsset");
		FILE* file = nullptr;

	#ifdef _WIN32
		_wfopen_s(&file, outPath.CStr(), L"wb");
	#else
		file = fopen(String::FromWide(outPath.CStr()).CStr(), "wb");
	#endif

		if (!file)
		{
			PLU_ERROR("Failed to open file for writing: {}", String::FromWide(outPath.CStr()).CStr());
			return false;
		}

		fwrite(&kAssetMagic, sizeof(UInt32), 1, file);
		fwrite(&kImageAssetVersion, sizeof(UInt32), 1, file);

		UInt32 typeLength = static_cast<UInt32>(strlen(kImageAssetTypeName));
		fwrite(&typeLength, sizeof(UInt32), 1, file);
		fwrite(kImageAssetTypeName, sizeof(char), typeLength, file);

		UInt64 uuid = PluUUID();
		fwrite(&uuid, sizeof(UInt64), 1, file);

		UInt32 width = image.GetWidth();
		UInt32 height = image.GetHeight();
		UInt32 channels = image.GetChannels();
		UInt8 grayscale = image.IsGrayscale() ? 1 : 0;
		UInt8 hasAlpha = image.HasAlpha() ? 1 : 0;
		fwrite(&width, sizeof(UInt32), 1, file);
		fwrite(&height, sizeof(UInt32), 1, file);
		fwrite(&channels, sizeof(UInt32), 1, file);
		fwrite(&grayscale, sizeof(UInt8), 1, file);
		fwrite(&hasAlpha, sizeof(UInt8), 1, file);

		fwrite(image.GetPixels()->Data(), sizeof(UInt8), image.GetMemorySize(), file);
		fclose(file);
		return true;
	}
}
}

bool Plu::TextureImport::ImportTexture(const PathW& origin, const PathW &outPath)
{
	PLU_PROFILE_SCOPE("TextureImport::ImportTexture");
	TOwningPointer<ImageInfo> image = ImageInfo::LoadFromFile(origin.ToString().ToNarrow());
	if (!image) {
		PLU_ERROR("Error importing texture {}!", origin.ToString().ToNarrow().CStr());
		return false;
	}
	return WriteImageAsset(*image, outPath);
}

bool Plu::TextureImport::ImportTextureFromMemory(const unsigned char* compressedData, UInt64 size, const PathW& outPath)
{
	PLU_PROFILE_SCOPE("TextureImport::ImportTextureFromMemory");
	TOwningPointer<ImageInfo> image = ImageInfo::LoadFromMemory(compressedData, size);
	if (!image) {
		PLU_ERROR("Error decoding embedded texture from memory!");
		return false;
	}
	return WriteImageAsset(*image, outPath);
}

Plu::TOwningPointer<Plu::ImageInfo> Plu::TextureImport::LoadTexture(const PathW &textPath)
{
	PLU_PROFILE_SCOPE("TextureImport::LoadTexture");
	FILE* file = nullptr;

#ifdef _WIN32
	_wfopen_s(&file, textPath.CStr(), L"rb");
#else
	file = fopen(String::FromWide(textPath.CStr()).CStr(), "rb");
#endif

	if (!file)
	{
		PLU_CORE_ERROR("Failed to open file: {}", String::FromWide(textPath.CStr()).CStr());
		return {};
	}

	auto fail = [&](const char* reason) -> TOwningPointer<ImageInfo> {
		PLU_ERROR("File {}: {}", String::FromWide(textPath.CStr()).CStr(), reason);
		fclose(file);
		return {};
	};

	UInt32 magic = 0;
	UInt32 version = 0;
	if (fread(&magic, sizeof(UInt32), 1, file) != 1 || fread(&version, sizeof(UInt32), 1, file) != 1
		|| magic != kAssetMagic || version != kImageAssetVersion)
	{
		return fail("invalid magic or version!");
	}

	constexpr UInt32 kMaxTypeNameLength = 1024;
	UInt32 typeLength = 0;
	if (fread(&typeLength, sizeof(UInt32), 1, file) != 1 || typeLength == 0 || typeLength > kMaxTypeNameLength)
	{
		return fail("invalid type name length!");
	}
	char typeName[kMaxTypeNameLength + 1] = {};
	if (fread(typeName, sizeof(char), typeLength, file) != typeLength || strcmp(typeName, kImageAssetTypeName) != 0)
	{
		return fail("is not an ImageInfo!");
	}

	UInt64 uuid = 0;
	UInt32 width = 0, height = 0, channels = 0;
	UInt8 grayscale = 0, hasAlpha = 0;
	bool headerRead = fread(&uuid, sizeof(UInt64), 1, file) == 1
				   && fread(&width, sizeof(UInt32), 1, file) == 1
				   && fread(&height, sizeof(UInt32), 1, file) == 1
				   && fread(&channels, sizeof(UInt32), 1, file) == 1
				   && fread(&grayscale, sizeof(UInt8), 1, file) == 1
				   && fread(&hasAlpha, sizeof(UInt8), 1, file) == 1;
	if (!headerRead || channels < 1 || channels > 4)
	{
		return fail("has a corrupted image header!");
	}

	UInt64 pixelCount = static_cast<UInt64>(width) * height * channels;
	PLU_INFO("Loading texture: W {}, H {}, C {}, Bytes {}", width, height, channels, pixelCount);
	DynamicArray<UInt8> pixels;
	pixels.Resize(pixelCount);
	if (fread(pixels.Data(), sizeof(UInt8), pixelCount, file) != pixelCount)
	{
		return fail("is truncated!");
	}
	fclose(file);

	TOwningPointer<ImageInfo> image = ImageInfo::FromPixels(width, height, channels,
		ImageContents{ grayscale != 0, hasAlpha != 0 }, std::move(pixels));
	image->Uuid = uuid;
	return image;
}
