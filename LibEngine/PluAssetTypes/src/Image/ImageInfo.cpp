//
// Created by Plutex on 11/21/26.
//

#include "PluEngine/AssetTypes/Image/ImageInfo.h"
#include <ImageMagick-7/Magick++.h>

#include "PluBase/Log.h"
#include "PluEngine/Timer.h"
#include "PluSTL/Pointers/Casts.h"

void Plu::InitializeImageRuntime() {
    Magick::InitializeMagick(nullptr);
}

namespace Plu
{
    static ImageContents AnalyzeImage(Magick::Image& img, UInt32 w, UInt32 h) {
        PLU_PROFILE_SCOPE("ImageInfo::AnalyzeImage");
        DynamicArray<UInt8> row;
        row.Resize(w * 4);
        ImageContents contents;

        for (UInt32 y = 0; y < h && (contents.Grayscale || !contents.HasAlpha); y++) {
            img.write(0, y, w, 1, "RGBA", Magick::CharPixel, row.Data());
            for (UInt32 x = 0; x < w; x++) {
                UInt8* pixel = &row[x * 4];
                contents.Grayscale = contents.Grayscale && pixel[0] == pixel[1] && pixel[1] == pixel[2];
                contents.HasAlpha = contents.HasAlpha || pixel[3] != 255;
            }
        }
        return contents;
    }

    // Shared tail of LoadFromFile / LoadFromMemory: `img` is already read.
    static TOwningPointer<ImageInfo> DecodeMagickImage(Magick::Image& img, const ImageLoadOptions& options) {
        Magick::ColorspaceType colorspace = img.colorSpace();
        if (colorspace != Magick::sRGBColorspace && colorspace != Magick::GRAYColorspace) {
            img.colorSpace(Magick::sRGBColorspace);
        }
        if (options.Flip) img.flip();

        UInt32 width = static_cast<UInt32>(img.columns());
        UInt32 height = static_cast<UInt32>(img.rows());
        ImageContents contents = AnalyzeImage(img, width, height);
        UInt32 channels = 4;
        if (options.OptimizeChannels) {
            channels = (contents.Grayscale ? 1 : 3) + (contents.HasAlpha ? 1 : 0);
        }

        DynamicArray<UInt8> pixels;
        pixels.Resize(static_cast<UInt64>(width) * height * channels);

        const char* maps[] = {"R", "RG", "RGB", "RGBA"};
        img.write(0, 0, width, height, maps[channels - 1], Magick::CharPixel, pixels.Data());
        PLU_CORE_INFO("New Image loaded using ImageMagick! W {}, H {}, Map {}", width, height, maps[channels - 1]);
        return ImageInfo::FromPixels(width, height, channels, contents, std::move(pixels));
    }
}

Plu::TOwningPointer<Plu::ImageInfo> Plu::ImageInfo::LoadFromFile(const Path& path, ImageLoader loader, const ImageLoadOptions& options) {
    PLU_PROFILE_SCOPE("ImageInfo::LoadFromFile");
    switch (loader) {
        case ImageLoader::Magick:
        {
            try {
                Magick::Image img(path.CStr());
                return DecodeMagickImage(img, options);
            } catch (Magick::Warning& w) {
                PLU_CORE_WARN("Warning while loading Image {}! {}", path.CStr(), w.what());
            } catch (Magick::Exception& e) {
                PLU_CORE_ERROR("Error while loading Image {}! {}", path.CStr(), e.what());
            }
            break;
        }
        case ImageLoader::STBI:
            PLU_CORE_ERROR("STB Image loading is not supported yet");
            break;
    }
    return {};
}

Plu::TOwningPointer<Plu::ImageInfo> Plu::ImageInfo::LoadFromMemory(const UInt8* data, UInt64 size, ImageLoader loader, const ImageLoadOptions& options) {
    PLU_PROFILE_SCOPE("ImageInfo::LoadFromMemory");
    switch (loader) {
        case ImageLoader::Magick:
        {
            try {
                Magick::Blob blob(data, size);
                Magick::Image img;
                img.read(blob);
                return DecodeMagickImage(img, options);
            } catch (Magick::Warning& w) {
                PLU_CORE_WARN("Warning while decoding Image from memory! {}", w.what());
            } catch (Magick::Exception& e) {
                PLU_CORE_ERROR("Error while decoding Image from memory! {}", e.what());
            }
            break;
        }
        case ImageLoader::STBI:
            PLU_CORE_ERROR("STB Image loading is not supported yet");
            break;
    }
    return {};
}

Plu::TOwningPointer<Plu::ImageInfo> Plu::ImageInfo::FromPixels(UInt32 width, UInt32 height, UInt32 channels, ImageContents contents, DynamicArray<UInt8>&& pixels) {
    PLU_CORE_ASSERT(channels >= 1 && channels <= 4, "Image channel count must be 1-4!");
    PLU_CORE_ASSERT(pixels.Size() == static_cast<UInt64>(width) * height * channels, "Image pixel buffer does not match its size!");
    TOwningPointer<ImageInfo> image = CreateOwning<ImageInfo>();
    image->mWidth = width;
    image->mHeight = height;
    image->mChannels = channels;
    image->mContents = contents;
    image->mPixels = std::move(pixels);
    return image;
}

DynamicArray<UInt8>* Plu::ImageInfo::GetPixels() {
    return &mPixels;
}

const DynamicArray<UInt8>* Plu::ImageInfo::GetPixels() const {
    return &mPixels;
}

UInt64 Plu::ImageInfo::GetMemorySize() const {
    return static_cast<UInt64>(mWidth) * mHeight * mChannels;
}

UInt32 Plu::ImageInfo::GetWidth() const {
    return mWidth;
}
UInt32 Plu::ImageInfo::GetHeight() const {
    return mHeight;
}
UInt32 Plu::ImageInfo::GetChannels() const {
    return mChannels;
}
bool Plu::ImageInfo::IsGrayscale() const {
    return mContents.Grayscale;
}
bool Plu::ImageInfo::HasAlpha() const {
    return mContents.HasAlpha;
}
