//
// Created by Plutex on 11/21/26.
//

#ifndef PLUENGINE_IMAGEINFO_H
#define PLUENGINE_IMAGEINFO_H

#include "PluEngine/Core.h"
#include "PluEngine/Core/IAssetData.h"
#include "PluSTL/Array/Array.h"
#include "PluSTL/Path/Path.h"
#include "PluSTL/Pointers/TOwningPointer.h"
#include "ImageInfo.generated.h"

namespace Plu
{
    // Initialises ImageMagick. Called once by Application::EngineInit, before any image is loaded.
    PLUASSETTYPES_API void InitializeImageRuntime();

    enum class ImageLoader
    {
        Magick,
        STBI
    };

    struct ImageLoadOptions
    {
        bool Flip = false;

        //Check for grayscale, alpha existence etc. all of this to reduce the RAM cost of the image
        //GPU in most cases will still convert image to RGBA, but the loading will be faster and drive will thank you
        bool OptimizeChannels = true;
    };

    struct ImageContents
    {
        bool Grayscale = true;
        bool HasAlpha = false;
    };

    // Decoded 8-bit image: tightly packed rows, top row first, GetChannels() bytes per pixel
    // (1 = R, 2 = RG, 3 = RGB, 4 = RGBA). The texture asset — its .plubin is written and read by
    // TextureImport in PluAssetPipeline.
    PLU_STRUCT()
    struct PLUASSETTYPES_API ImageInfo : IAssetData
    {
        REFLECTION_BODY_IMAGEINFO()

    private:
        UInt32 mChannels = 0;
        UInt32 mWidth = 0;
        UInt32 mHeight = 0;
        DynamicArray<UInt8> mPixels;

        ImageContents mContents;

    public:
        ImageInfo() = default;
        ~ImageInfo() override = default;

        // Both return null (and log) when the image cannot be decoded.
        static TOwningPointer<ImageInfo> LoadFromFile(const Path& path, ImageLoader loader = ImageLoader::Magick, const ImageLoadOptions& options = {});
        // Decodes an encoded blob (PNG, JPG, ...) — e.g. a texture embedded in a model file.
        static TOwningPointer<ImageInfo> LoadFromMemory(const UInt8* data, UInt64 size, ImageLoader loader = ImageLoader::Magick, const ImageLoadOptions& options = {});
        // Wraps already decoded pixels; `pixels` must hold width * height * channels bytes.
        static TOwningPointer<ImageInfo> FromPixels(UInt32 width, UInt32 height, UInt32 channels, ImageContents contents, DynamicArray<UInt8>&& pixels);

        [[nodiscard]] DynamicArray<UInt8>* GetPixels();
        [[nodiscard]] const DynamicArray<UInt8>* GetPixels() const;

        [[nodiscard]] UInt64 GetMemorySize() const;

        [[nodiscard]] UInt32 GetWidth() const;
        [[nodiscard]] UInt32 GetHeight() const;
        [[nodiscard]] UInt32 GetChannels() const;

        [[nodiscard]] bool IsGrayscale() const;
        [[nodiscard]] bool HasAlpha() const;
    };
}


#endif //PLUENGINE_IMAGEINFO_H
