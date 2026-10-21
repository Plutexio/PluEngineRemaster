//
// Created by Plutex on 2026-08-02.
//

#ifndef PLUENGINE_GLSAMPLEROBJECT_H
#define PLUENGINE_GLSAMPLEROBJECT_H

#include <glad/glad.h>
#include "PluEngine/Core.h"
#include "PluBase/Log.h"

namespace Plu
{
    // GL sampler object wrapper: filtering / wrapping / comparison state that lives on the
    // TEXTURE UNIT instead of on the texture.
    //
    // Same shape as ShaderStorageBuffer / UniformBuffer (move-only header-only GL resource
    // wrapper). The point of a sampler object is that one texture can be read two different
    // ways: the CSM shadow array is sampled through a comparison sampler during the lighting
    // pass (which is what turns `texture()` into hardware PCF) while staying a plain depth
    // texture for anything else that binds it — a debug texture viewer, a blit, a copy.
    //
    // A bound sampler overrides the texture's own parameters until it is unbound, so it must be
    // unbound before handing the unit to code that expects raw texture state (the ImGui backend
    // in particular).
    class SamplerObject
    {
    public:
        SamplerObject()
            : SamplerID(0)
        {
        }

        ~SamplerObject()
        {
            Destroy();
        }

        // Prevent copying
        SamplerObject(const SamplerObject&) = delete;
        SamplerObject& operator=(const SamplerObject&) = delete;

        // Allow moving
        SamplerObject(SamplerObject&& Other) noexcept
            : SamplerID(Other.SamplerID)
        {
            Other.SamplerID = 0;
        }

        SamplerObject& operator=(SamplerObject&& Other) noexcept
        {
            if (this != &Other)
            {
                Destroy();
                SamplerID = Other.SamplerID;
                Other.SamplerID = 0;
            }
            return *this;
        }

        bool Create()
        {
            Destroy();
            glGenSamplers(1, &SamplerID);
            return CheckGLError("SamplerObject::Create");
        }

        void SetFilter(GLenum MinFilter, GLenum MagFilter) const
        {
            if (!IsValid()) return;
            glSamplerParameteri(SamplerID, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(MinFilter));
            glSamplerParameteri(SamplerID, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(MagFilter));
        }

        // BorderColor is only consulted by the GL_CLAMP_TO_BORDER wrap modes; pass null to skip it.
        void SetWrap(GLenum WrapS, GLenum WrapT, const float* BorderColor = nullptr) const
        {
            if (!IsValid()) return;
            glSamplerParameteri(SamplerID, GL_TEXTURE_WRAP_S, static_cast<GLint>(WrapS));
            glSamplerParameteri(SamplerID, GL_TEXTURE_WRAP_T, static_cast<GLint>(WrapT));
            if (BorderColor != nullptr)
            {
                glSamplerParameterfv(SamplerID, GL_TEXTURE_BORDER_COLOR, BorderColor);
            }
        }

        // Turn on depth comparison: a shadow sampler in GLSL then returns the filtered result of
        // `CompareFunc(reference, storedDepth)` rather than the depth itself. Combined with a
        // LINEAR filter this is hardware 2x2 PCF — one fetch instead of four manual taps.
        void SetCompareMode(GLenum CompareFunc) const
        {
            if (!IsValid()) return;
            glSamplerParameteri(SamplerID, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glSamplerParameteri(SamplerID, GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>(CompareFunc));
        }

        void DisableCompareMode() const
        {
            if (!IsValid()) return;
            glSamplerParameteri(SamplerID, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        }

        void Bind(GLuint TextureUnit) const
        {
            glBindSampler(TextureUnit, SamplerID);
        }

        // Hand the unit back to plain texture state. Static because it must be callable without
        // an object — code that did not bind the sampler still has to be able to clear it.
        static void Unbind(GLuint TextureUnit)
        {
            glBindSampler(TextureUnit, 0);
        }

        [[nodiscard]] GLuint GetID() const { return SamplerID; }
        [[nodiscard]] bool IsValid() const { return SamplerID != 0; }

        void Destroy()
        {
            if (SamplerID != 0)
            {
                glDeleteSamplers(1, &SamplerID);
                SamplerID = 0;
            }
        }

    private:
        GLuint SamplerID;

        // Drains the GL error queue after an operation, logging any errors. Returns true if clean.
        static bool CheckGLError(const char* Where)
        {
            bool Clean = true;
            GLenum Err;
            while ((Err = glGetError()) != GL_NO_ERROR)
            {
                Clean = false;
                const char* Msg;
                switch (Err)
                {
                    case GL_INVALID_ENUM:      Msg = "INVALID_ENUM"; break;
                    case GL_INVALID_VALUE:     Msg = "INVALID_VALUE"; break;
                    case GL_INVALID_OPERATION: Msg = "INVALID_OPERATION"; break;
                    case GL_OUT_OF_MEMORY:     Msg = "OUT_OF_MEMORY"; break;
                    default:                   Msg = "UNKNOWN"; break;
                }
                PLU_CORE_ERROR("OpenGL Error at {}: {} (0x{:x})", Where, Msg, Err);
            }
            return Clean;
        }
    };
}

#endif //PLUENGINE_GLSAMPLEROBJECT_H
