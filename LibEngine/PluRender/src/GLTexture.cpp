//
// Created by Plutex on 2026-02-09.
//

#include "PluEngine/Render/GLTexture.h"
#include <algorithm>

#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image_write.h"

namespace Plu
{
    void CheckGLError(const char* where)
    {
        GLenum err;
        while ((err = glGetError()) != GL_NO_ERROR)
        {
            const char* error;
            switch(err)
            {
                case GL_INVALID_ENUM: error = "INVALID_ENUM"; break;
                case GL_INVALID_VALUE: error = "INVALID_VALUE"; break;
                case GL_INVALID_OPERATION: error = "INVALID_OPERATION"; break;
                case GL_OUT_OF_MEMORY: error = "OUT_OF_MEMORY"; break;
                default: error = "UNKNOWN"; break;
            }
            PLU_CORE_ERROR("OpenGL Error at {}: {} (0x{})", where, error, err);
        }
    }

    Texture::Texture()
        : TextureID(0)
        , Target(GL_TEXTURE_2D)
        , Width(0)
        , Height(0)
        , Layers(1)
        , Channels(0)
        , BaseMipLevel(0)
        , MaxMipLevel(1000)
        , MipLevelCount(1)
        , bIsDepth(false)
    {
    }

    Texture::~Texture()
    {
        Destroy();
    }

    Texture::Texture(Texture&& Other) noexcept
        : TextureID(Other.TextureID)
        , Target(Other.Target)
        , Width(Other.Width)
        , Height(Other.Height)
        , Layers(Other.Layers)
        , Channels(Other.Channels)
        , BaseMipLevel(Other.BaseMipLevel)
        , MaxMipLevel(Other.MaxMipLevel)
        , MipLevelCount(Other.MipLevelCount)
        , bIsDepth(Other.bIsDepth)
    {
        Other.TextureID = 0;
        Other.Target = GL_TEXTURE_2D;
        Other.Width = 0;
        Other.Height = 0;
        Other.Layers = 1;
        Other.Channels = 0;
        Other.BaseMipLevel = 0;
        Other.MaxMipLevel = 1000;
        Other.MipLevelCount = 1;
        Other.bIsDepth = false;
    }

    Texture& Texture::operator=(Texture&& Other) noexcept
    {
        if (this != &Other)
        {
            Destroy();

            TextureID = Other.TextureID;
            Target = Other.Target;
            Width = Other.Width;
            Height = Other.Height;
            Layers = Other.Layers;
            Channels = Other.Channels;
            BaseMipLevel = Other.BaseMipLevel;
            MaxMipLevel = Other.MaxMipLevel;
            MipLevelCount = Other.MipLevelCount;
            bIsDepth = Other.bIsDepth;

            Other.TextureID = 0;
            Other.Target = GL_TEXTURE_2D;
            Other.Width = 0;
            Other.Height = 0;
            Other.Layers = 1;
            Other.Channels = 0;
            Other.BaseMipLevel = 0;
            Other.MaxMipLevel = 1000;
            Other.MipLevelCount = 1;
            Other.bIsDepth = false;
        }
        return *this;
    }

    bool Texture::CreateFromInfo(TextureInfo* Info, bool GenerateMipmaps)
    {
        if (Info->RawData.Data == nullptr || Info->RawData.Width <= 0 || Info->RawData.Height <= 0)
        {
            return false;
        }

        Target = GL_TEXTURE_2D;
        Width = Info->RawData.Width;
        Height = Info->RawData.Height;
        Layers = 1;
        Channels = Info->RawData.Channels;

        glGenTextures(1, &TextureID);
        glBindTexture(GL_TEXTURE_2D, TextureID);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        // Set default texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        if (!GenerateMipmaps)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // Bez MIPMAP
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        else
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }

        // Upload texture data
        glTexImage2D(GL_TEXTURE_2D, 0, GetInternalFormat(), Width, Height, 0,
                     GetFormat(), GL_UNSIGNED_BYTE, Info->RawData.Data);

        CheckGLError("After glTexImage2D");

        if (GenerateMipmaps)
        {
            glGenerateMipmap(GL_TEXTURE_2D);
            MipLevelCount = CalculateMipLevelCount();
        }
        else
        {
            MipLevelCount = 1;
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        PLU_CORE_INFO("Texture Created with Major Success!");
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

        return true;
    }

    bool Texture::Create(Int32 InWidth, Int32 InHeight, Int32 InChannels, bool GenerateMipmaps)
    {
        Target = GL_TEXTURE_2D;
        Width = InWidth;
        Height = InHeight;
        Layers = 1;
        Channels = InChannels;

        glGenTextures(1, &TextureID);
        glBindTexture(GL_TEXTURE_2D, TextureID);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        if (GenerateMipmaps)
        {
            MipLevelCount = CalculateMipLevelCount();
            for (Int32 Level = 0; Level < MipLevelCount; ++Level)
            {
                Int32 MipWidth = GetMipWidth(Level);
                Int32 MipHeight = GetMipHeight(Level);
                glTexImage2D(GL_TEXTURE_2D, Level, GetInternalFormat(), MipWidth, MipHeight, 0,
                             GetFormat(), GL_UNSIGNED_BYTE, nullptr);
            }
        }
        else
        {
            MipLevelCount = 1;
            glTexImage2D(GL_TEXTURE_2D, 0, GetInternalFormat(), Width, Height, 0,
                         GetFormat(), GL_UNSIGNED_BYTE, nullptr);
        }

        glBindTexture(GL_TEXTURE_2D, 0);

        return true;
    }

    bool Texture::StreamMipLevel(Int32 MipLevel, const unsigned char* Data)
    {
        PLU_CORE_ASSERT(Target == GL_TEXTURE_2D, "Texture::StreamMipLevel is 2D-only (mip streaming has no array path)");
        if (!IsValid() || Data == nullptr || MipLevel < 0 || MipLevel >= MipLevelCount)
        {
            return false;
        }

        Int32 MipWidth = GetMipWidth(MipLevel);
        Int32 MipHeight = GetMipHeight(MipLevel);

        glBindTexture(GL_TEXTURE_2D, TextureID);
        glTexSubImage2D(GL_TEXTURE_2D, MipLevel, 0, 0, MipWidth, MipHeight,
                        GetFormat(), GL_UNSIGNED_BYTE, Data);
        glBindTexture(GL_TEXTURE_2D, 0);

        return true;
    }

    bool Texture::StreamMipLevel(Int32 MipLevel, Int32 MipWidth, Int32 MipHeight, const unsigned char* Data)
    {
        PLU_CORE_ASSERT(Target == GL_TEXTURE_2D, "Texture::StreamMipLevel is 2D-only (mip streaming has no array path)");
        if (!IsValid() || Data == nullptr || MipLevel < 0)
        {
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, TextureID);

        // Reallocate if dimensions don't match
        if (MipLevel == 0)
        {
            Width = MipWidth;
            Height = MipHeight;
        }

        glTexImage2D(GL_TEXTURE_2D, MipLevel, GetInternalFormat(), MipWidth, MipHeight, 0,
                     GetFormat(), GL_UNSIGNED_BYTE, Data);

        glBindTexture(GL_TEXTURE_2D, 0);

        return true;
    }

    void Texture::AllocateMipLevels(Int32 NumLevels)
    {
        PLU_CORE_ASSERT(Target == GL_TEXTURE_2D, "Texture::AllocateMipLevels is 2D-only (mip streaming has no array path)");
        if (!IsValid() || NumLevels < 1)
        {
            return;
        }

        MipLevelCount = NumLevels;

        glBindTexture(GL_TEXTURE_2D, TextureID);

        for (Int32 Level = 0; Level < MipLevelCount; ++Level)
        {
            Int32 MipWidth = GetMipWidth(Level);
            Int32 MipHeight = GetMipHeight(Level);
            glTexImage2D(GL_TEXTURE_2D, Level, GetInternalFormat(), MipWidth, MipHeight, 0,
                         GetFormat(), GL_UNSIGNED_BYTE, nullptr);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void Texture::SetBaseMipLevel(Int32 Level)
    {
        if (!IsValid() || Level < 0)
        {
            return;
        }

        BaseMipLevel = Level;

        glBindTexture(Target, TextureID);
        glTexParameteri(Target, GL_TEXTURE_BASE_LEVEL, BaseMipLevel);
        glBindTexture(Target, 0);
    }

    void Texture::SetMaxMipLevel(Int32 Level)
    {
        if (!IsValid() || Level < 0)
        {
            return;
        }

        MaxMipLevel = Level;

        glBindTexture(Target, TextureID);
        glTexParameteri(Target, GL_TEXTURE_MAX_LEVEL, MaxMipLevel);
        glBindTexture(Target, 0);
    }

    Int32 Texture::GetMipWidth(Int32 MipLevel) const
    {
        return std::max(1, Width >> MipLevel);
    }

    Int32 Texture::GetMipHeight(Int32 MipLevel) const
    {
        return std::max(1, Height >> MipLevel);
    }

    void Texture::Bind(GLuint TextureUnit) const
    {
        glActiveTexture(GL_TEXTURE0 + TextureUnit);
        glBindTexture(Target, TextureID);
    }

    void Texture::Unbind() const
    {
        glBindTexture(Target, 0);
    }

    void Texture::SetWrapMode(GLenum WrapS, GLenum WrapT)
    {
        if (!IsValid())
        {
            return;
        }

        glBindTexture(Target, TextureID);
        glTexParameteri(Target, GL_TEXTURE_WRAP_S, WrapS);
        glTexParameteri(Target, GL_TEXTURE_WRAP_T, WrapT);
        glBindTexture(Target, 0);
    }

    void Texture::SetFilterMode(GLenum MinFilter, GLenum MagFilter)
    {
        if (!IsValid())
        {
            return;
        }

        glBindTexture(Target, TextureID);
        glTexParameteri(Target, GL_TEXTURE_MIN_FILTER, MinFilter);
        glTexParameteri(Target, GL_TEXTURE_MAG_FILTER, MagFilter);
        glBindTexture(Target, 0);
    }

    void Texture::Destroy()
    {
        if (TextureID != 0)
        {
            glDeleteTextures(1, &TextureID);
            TextureID = 0;
            Target = GL_TEXTURE_2D;
            Width = 0;
            Height = 0;
            Layers = 1;
            Channels = 0;
            BaseMipLevel = 0;
            MaxMipLevel = 1000;
            MipLevelCount = 1;
            bIsDepth = false;
        }
    }

    void Texture::SaveTexture(Path path)
    {
        PLU_CORE_ASSERT(Target == GL_TEXTURE_2D, "Texture::SaveTexture is 2D-only (glGetTexImage would need a per-layer path)");
        Bind();
        DynamicArray<unsigned char> pixels;
        pixels.Resize(Width * Height * Channels);
        glGetTexImage(GL_TEXTURE_2D, 0, GetFormat(), GL_UNSIGNED_BYTE, pixels.Data());

        DynamicArray<unsigned char> flipped(pixels.Size());
        for (int y = 0; y < Height; y++) {
            memcpy(
                flipped.Data() + y * Width * Channels,
                pixels.Data() + (Height - 1 - y) * Width * Channels,
                Width * Channels
            );
        }

        Path finalPath = path.HasFilename() ? path : path.ToString() + String::FromInt(TextureID) + ".png";
        stbi_write_png(finalPath.ToString().CStr(), Width, Height, Channels, flipped.Data(), Width * Channels);

        Unbind();
    }

    bool Texture::CreateDepth(Int32 InWidth, Int32 InHeight, bool WithStencil, bool Use16Bit)
    {
        if (InWidth <= 0 || InHeight <= 0)
        {
            return false;
        }

        Target = GL_TEXTURE_2D;
        Width = InWidth;
        Height = InHeight;
        Layers = 1;
        Channels = 0;
        MipLevelCount = 1;
        bIsDepth = true;

        // Wariant bez stencila (mapy cieni, FrameBufferType::DepthOnly) domyślnie używa
        // 32-bitowej głębi float zamiast 24-bit unorm — więcej bitów na ten sam zakres z ortho
        // = mniej acne z kwantyzacji głębi. Use16Bit (mapy cieni kaskad) tnie to do D16 unorm:
        // przy ciasnym per-kaskadowym zakresie z kwantyzacja to pojedyncze mm, a slope-scaled
        // polygon offset po stronie castera i tak skaluje się z precyzją bufora.
        GLenum InternalFormat = WithStencil ? GL_DEPTH24_STENCIL8 : (Use16Bit ? GL_DEPTH_COMPONENT16 : GL_DEPTH_COMPONENT32F);
        GLenum Format         = WithStencil ? GL_DEPTH_STENCIL     : GL_DEPTH_COMPONENT;
        GLenum DataType       = WithStencil ? GL_UNSIGNED_INT_24_8 : (Use16Bit ? GL_UNSIGNED_SHORT : GL_FLOAT);

        glGenTextures(1, &TextureID);
        glBindTexture(GL_TEXTURE_2D, TextureID);

        glTexImage2D(GL_TEXTURE_2D, 0, InternalFormat, Width, Height, 0,
                     Format, DataType, nullptr);

        // Depth textures should never use mipmaps or linear filtering
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        // Clamp to border — values outside [0,1] UV return max depth (1.0),
        // which is the correct behaviour for shadow maps
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float BorderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, BorderColor);

        glBindTexture(GL_TEXTURE_2D, 0);

        CheckGLError("Texture::CreateDepth");
        return true;
    }

    bool Texture::CreateDepthArray(Int32 InWidth, Int32 InHeight, Int32 InLayers, bool Use16Bit)
    {
        if (InWidth <= 0 || InHeight <= 0 || InLayers <= 0)
        {
            PLU_CORE_ERROR("Texture::CreateDepthArray - Invalid dimensions: {}x{}x{}", InWidth, InHeight, InLayers);
            return false;
        }

        Destroy();

        Target = GL_TEXTURE_2D_ARRAY;
        Width = InWidth;
        Height = InHeight;
        Layers = InLayers;
        Channels = 0;
        MipLevelCount = 1;
        bIsDepth = true;

        // Same format choice as CreateDepth — D32F by default, D16 on request.
        const GLenum InternalFormat = Use16Bit ? GL_DEPTH_COMPONENT16 : GL_DEPTH_COMPONENT32F;
        const GLenum DataType       = Use16Bit ? GL_UNSIGNED_SHORT    : GL_FLOAT;

        glGenTextures(1, &TextureID);
        glBindTexture(GL_TEXTURE_2D_ARRAY, TextureID);

        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, InternalFormat, Width, Height, Layers, 0,
                     GL_DEPTH_COMPONENT, DataType, nullptr);

        // Nearest + clamp-to-border with a white border, exactly like CreateDepth: sampling
        // outside [0,1] returns max depth, which reads as "lit". The compare mode and the
        // linear filtering that hardware PCF needs are NOT set here on purpose — they live on
        // a SamplerObject bound only for the lighting pass, so the texture object itself stays
        // a plain depth texture that debug tools (TextureViewerPanel) can still display.
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float BorderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, BorderColor);

        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

        CheckGLError("Texture::CreateDepthArray");
        return true;
    }

    GLenum Texture::GetInternalFormat() const
    {
        switch (Channels)
        {
            case 1: return GL_R8;
            case 2: return GL_RG8;
            case 3: return GL_RGB8;   // ⭐ Zamiast GL_RGB
            case 4: return GL_RGBA8;  // ⭐ Zamiast GL_RGBA
            default: return GL_RGBA8;
        }
    }

    GLenum Texture::GetFormat() const
    {
        switch (Channels)
        {
            case 1: return GL_RED;
            case 2: return GL_RG;
            case 3: return GL_RGB;
            case 4: return GL_RGBA;
            default: return GL_RGBA;
        }
    }

    Int32 Texture::CalculateMipLevelCount() const
    {
        Int32 MaxDim = std::max(Width, Height);
        Int32 Levels = 1;

        while (MaxDim > 1)
        {
            MaxDim >>= 1;
            ++Levels;
        }

        return Levels;
    }
}
